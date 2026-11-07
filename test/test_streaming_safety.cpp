#include "test.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <memory>

namespace {

struct ElementText {
	std::string name;
	std::string text;
};

std::vector<ElementText> read_chunks(std::vector<std::string> const &chunks)
{
	xstream::Reader reader;
	size_t chunk = 0;
	reader.parse([&] {
		if (chunk < chunks.size()) reader.input(chunks[chunk++]);
		else reader.finish_input();
	});
	std::vector<ElementText> elements;
	size_t steps = 0;
	while (reader.next()) {
		if (++steps > 10000) {
			ADD_FAILURE() << "Reader did not stop";
			break;
		}
		EXPECT_NE(reader.state(), xstream::Reader::None);
		if (reader.match_end(reader.path().c_str())) {
			elements.push_back({reader.name(), reader.text()});
		}
	}
	EXPECT_EQ(chunk, chunks.size());
	EXPECT_EQ(reader.result(), xstream::Reader::ReadResult::EndDocument);
	return elements;
}

std::string decoded(std::vector<char> const &value)
{
	return {value.begin(), value.end()};
}

} // namespace

TEST(StreamingSafety, TextSurvivesBufferReuseAndReallocation)
{
	for (size_t length : {size_t(1), size_t(300)}) {
		SCOPED_TRACE(length);
		std::string child_text(length, 'x');
		auto elements = read_chunks({"<root>hello<child>", child_text + "</child></root>"});
		ASSERT_EQ(elements.size(), 2u);
		EXPECT_EQ(elements[0].name, "child");
		EXPECT_EQ(elements[0].text, child_text);
		EXPECT_EQ(elements[1].name, "root");
		EXPECT_EQ(elements[1].text, "hello");
	}
}

TEST(StreamingSafety, MixedTextAndCdataSurviveBufferChanges)
{
	std::string child_text(500, 'x');
	auto elements = read_chunks({
		"<root>before&amp;<![CDATA[<raw>]]><child>",
		child_text + "</child>",
		"after</root>"
	});
	ASSERT_EQ(elements.size(), 2u);
	EXPECT_EQ(elements[0].text, child_text);
	EXPECT_EQ(elements[1].text, "before&<raw>after");
}

TEST(StreamingSafety, NameAndAttributesSurviveInput)
{
	for (size_t length : {size_t(1), size_t(300)}) {
		SCOPED_TRACE(length);
		xstream::Reader reader;
		reader.parse([] {});
		reader.input("<root attr='value'>");
		ASSERT_TRUE(reader.next());
		ASSERT_TRUE(reader.match_start("/root"));
		reader.input(std::string(length, 'x') + "</root>");
		EXPECT_EQ(reader.name(), "root");
		EXPECT_TRUE(reader.is_start_element("root"));
		EXPECT_EQ(reader.attribute("attr", {}), "value");
	}
}

TEST(StreamingSafety, ReturnedCharactersOwnTheirData)
{
	xstream::Reader reader;
	reader.parse([] {});
	reader.input("<root>hello&amp;<![CDATA[<raw>]]><child>");
	ASSERT_TRUE(reader.next());
	ASSERT_TRUE(reader.next());
	ASSERT_TRUE(reader.is_characters());
	auto text = reader.characters();
	ASSERT_TRUE(reader.next());
	ASSERT_TRUE(reader.is_characters());
	auto cdata = reader.characters();
	ASSERT_TRUE(reader.next());
	ASSERT_TRUE(reader.match_start("/root/child"));
	reader.input(std::string(500, 'x') + "</child></root>");
	reader.parse([] {}); // Also release all Reader-owned character parts.
	EXPECT_EQ(decoded(text.decode()), "hello&");
	EXPECT_EQ(decoded(cdata.decode()), "<raw>");
}

TEST(StreamingSafety, TextAndAttributesAcrossEverySplit)
{
	std::string xml = "<root>hello&amp;<child attr='value'>world</child>tail</root>";
	for (size_t split = 0; split <= xml.size(); ++split) {
		SCOPED_TRACE(split);
		std::vector<std::string> chunks;
		if (split != 0) chunks.push_back(xml.substr(0, split));
		if (split != xml.size()) chunks.push_back(xml.substr(split));
		auto elements = read_chunks(chunks);
		ASSERT_EQ(elements.size(), 2u);
		EXPECT_EQ(elements[0].text, "world");
		EXPECT_EQ(elements[1].text, "hello&tail");
	}
	std::vector<std::string> bytes;
	for (char c : xml) bytes.emplace_back(1, c);
	auto elements = read_chunks(bytes);
	ASSERT_EQ(elements.size(), 2u);
	EXPECT_EQ(elements[0].text, "world");
	EXPECT_EQ(elements[1].text, "hello&tail");
}

TEST(StreamingSafety, CopiedReaderKeepsItsOwnInputPositions)
{
	xstream::Reader copy;
	{
		xstream::Reader original;
		original.parse([] {});
		original.input("<root>hello</root>");
		ASSERT_TRUE(original.next());
		copy = original;
	}
	ASSERT_TRUE(copy.next());
	ASSERT_TRUE(copy.is_characters());
	EXPECT_EQ(copy.text(), "hello");
	ASSERT_TRUE(copy.next());
	EXPECT_EQ(copy.name(), "root");
	EXPECT_EQ(copy.text(), "hello");
}

TEST(StreamingSafety, EmptyInputsAndShortPrefixesAreSafe)
{
	xstream::Reader default_reader;
	EXPECT_FALSE(default_reader.next());
	xstream::Reader empty{std::string_view{}};
	EXPECT_FALSE(empty.next());
	xstream::Reader empty_range(nullptr, nullptr);
	EXPECT_FALSE(empty_range.next());
	xstream::Reader empty_length(nullptr, size_t(0));
	EXPECT_FALSE(empty_length.next());
	EXPECT_TRUE(xstream::html_encode({}).empty());
	EXPECT_TRUE(xstream::html_decode({}).empty());

	for (std::string const xml : {"<root a='b'>text</root>", "<root><!--comment--></root>",
		"<root><![CDATA[text]]></root>"}) {
		for (size_t length = 0; length <= xml.size(); ++length) {
			SCOPED_TRACE(xml.substr(0, length));
			// No NUL terminator or readable bytes beyond the supplied range.
			auto storage = std::make_unique<char[]>(length);
			std::copy_n(xml.data(), length, storage.get());
			xstream::Reader reader(std::string_view(storage.get(), length));
			size_t steps = 0;
			while (reader.next()) {
				ASSERT_LT(++steps, 100u);
				(void)reader.name();
				(void)reader.text();
			}
		}
	}
}

TEST(EntitySafety, DecodeDoesNotRequireNulTermination)
{
	for (std::string const input : {"&", "&x", "&amp;", "abc&amp;def", "&;"}) {
		auto storage = std::make_unique<char[]>(input.size());
		std::copy(input.begin(), input.end(), storage.get());
		auto view = std::string_view(storage.get(), input.size());
		if (input == "&" || input == "&x" || input == "&;") {
			std::string output;
			EXPECT_FALSE(xstream::html_decode(view, &output));
		} else EXPECT_NO_THROW(xstream::html_decode(view));
	}
}

TEST(EntitySafety, DecodeDoesNotReadPastView)
{
	std::string input = "hello&amp;outside";
	std::string output;
	EXPECT_FALSE(xstream::html_decode(std::string_view(input.data(), 9), &output));
	EXPECT_EQ(xstream::html_decode(std::string_view(input.data(), 10)), "hello&");
	EXPECT_EQ(xstream::html_decode("&amp;&lt;&gt;&quot;&apos;&#65;"), "&<>\"'A");
}

TEST(EntitySafety, OversizedDecimalReferenceDoesNotOverflow)
{
	std::string output;
	EXPECT_FALSE(xstream::html_decode("&#" + std::string(1000, '9') + ";", &output));
}
