#include "test.h"
#include <gtest/gtest.h>
#include <tuple>

namespace {

using Reader = xstream::Reader;
using Event = std::tuple<Reader::StateType, std::string, std::string>;

std::vector<Event> collect(Reader *reader)
{
	std::vector<Event> events;
	while (reader->next()) {
		if (events.size() >= 100) {
			ADD_FAILURE() << "Reader did not stop";
			break;
		}
		events.emplace_back(reader->state(), reader->name(), reader->path());
	}
	return events;
}

struct InvalidXml {
	std::string xml;
	size_t events;
	size_t end_events;
	std::string path;
};

std::vector<InvalidXml> invalid_documents()
{
	return {
		{"<root><child></root>", 2, 0, "/root/child"},
		{"<root></wrong>", 1, 0, "/root"},
		{"</root>", 0, 0, ""},
		{"<root/></root>", 2, 1, ""},
		{"<root></root></root>", 2, 1, ""},
		{"<root></ROOT>", 1, 0, "/root"},
		{"<root><item></itemExtra></root>", 2, 0, "/root/item"},
		{"<root><ns:item></item>", 2, 0, "/root/ns:item"},
		{"<?xml version='1.0'?></xml>", 1, 0, ""},
		{"<root></root/>", 1, 0, "/root"}
	};
}

} // namespace

TEST(EndTags, RejectMismatchedAndExtraClosingTags)
{
	for (auto const &document : invalid_documents()) {
		SCOPED_TRACE(document.xml);
		Reader reader(document.xml);
		auto events = collect(&reader);
		EXPECT_EQ(reader.state(), Reader::Error);
		EXPECT_FALSE(reader.is_not_enough_input());
		EXPECT_EQ(reader.path(), document.path);
		EXPECT_EQ(events.size(), document.events);
		size_t end_events = 0;
		for (auto const &event : events) {
			if (std::get<0>(event) == Reader::EndElement) ++end_events;
		}
		EXPECT_EQ(end_events, document.end_events);
		EXPECT_FALSE(reader.is_end_element());
		EXPECT_FALSE(reader.match_end(reader.path().c_str()));
	}
}

TEST(EndTags, DoNotSearchAncestorsOrPopUnmatchedElements)
{
	Reader reader("<a><b><c></a>");
	ASSERT_EQ(collect(&reader).size(), 3u);
	EXPECT_EQ(reader.state(), Reader::Error);
	EXPECT_EQ(reader.path(), "/a/b/c");
	EXPECT_EQ(reader.depth(), 4);
}

TEST(EndTags, IdenticalNamesCloseOnlyTheInnermostElement)
{
	Reader reader("<a id='outer'><a id='middle'><a id='inner'/></a></a>");
	std::vector<std::string> paths;
	std::vector<std::string> ids;
	while (reader.next()) {
		if (reader.is_end_element()) {
			paths.push_back(reader.path());
			ids.push_back(reader.attribute("id", {}));
		}
	}
	EXPECT_EQ(paths, (std::vector<std::string>{"/a/a/a", "/a/a", "/a"}));
	EXPECT_EQ(ids, (std::vector<std::string>{"inner", "middle", "outer"}));
	EXPECT_NE(reader.state(), Reader::Error);
	EXPECT_EQ(reader.depth(), 1);
}

TEST(EndTags, ValidNamesAndTrailingWhitespaceStillWork)
{
	Reader reader("<root><ns:item></ns:item ><Item></Item></root>");
	auto events = collect(&reader);
	ASSERT_EQ(events.size(), 6u);
	EXPECT_EQ(std::get<2>(events[2]), "/root/ns:item");
	EXPECT_EQ(std::get<2>(events[4]), "/root/Item");
	EXPECT_EQ(std::get<2>(events[5]), "/root");
	EXPECT_NE(reader.state(), Reader::Error);
	EXPECT_EQ(reader.depth(), 1);
}

TEST(EndTags, EverySplitAndBytewiseInputRejectTheSameTags)
{
	for (auto const &document : invalid_documents()) {
		SCOPED_TRACE(document.xml);
		Reader one_shot(document.xml);
		auto expected = collect(&one_shot);
		for (size_t split = 1; split < document.xml.size(); ++split) {
			SCOPED_TRACE(split);
			std::vector<std::string> chunks = {
				document.xml.substr(0, split), document.xml.substr(split)
			};
			size_t pos = 0;
			Reader reader;
			reader.parse([&] { if (pos < chunks.size()) reader.input(chunks[pos++]); });
			EXPECT_EQ(collect(&reader), expected);
			EXPECT_EQ(reader.state(), Reader::Error);
			EXPECT_FALSE(reader.is_not_enough_input());
			EXPECT_EQ(reader.path(), document.path);
			EXPECT_EQ(reader.depth(), one_shot.depth());
		}
		size_t pos = 0;
		Reader reader;
		reader.parse([&] {
			if (pos < document.xml.size()) {
				reader.input(std::string_view(document.xml.data() + pos++, 1));
			}
		});
		EXPECT_EQ(collect(&reader), expected);
		EXPECT_EQ(reader.state(), Reader::Error);
		EXPECT_EQ(reader.path(), document.path);
	}
}

TEST(EndTags, PartialClosingTagWaitsBeforeCheckingItsName)
{
	for (bool matches : {false, true}) {
		Reader reader;
		reader.parse([] {});
		reader.input("<root>");
		ASSERT_TRUE(reader.next());
		reader.input(matches ? "</roo" : "</wro");
		EXPECT_FALSE(reader.next());
		EXPECT_EQ(reader.state(), Reader::None);
		EXPECT_TRUE(reader.is_not_enough_input());
		EXPECT_EQ(reader.path(), "/root");
		EXPECT_EQ(reader.depth(), 2);
		reader.input(matches ? "t>" : "ng>");
		EXPECT_EQ(reader.next(), matches);
		EXPECT_EQ(reader.state(), matches ? Reader::EndElement : Reader::Error);
		EXPECT_FALSE(reader.is_not_enough_input());
	}
}

TEST(EndTags, ErrorStopsInputAndPersistsUntilParseResetsIt)
{
	std::string invalid = "<root></wrong>";
	std::string xml = invalid + "<later/>";
	size_t pos = 0;
	size_t calls = 0;
	Reader reader;
	reader.parse([&] {
		++calls;
		if (pos < xml.size()) reader.input(std::string_view(xml.data() + pos++, 1));
	});
	ASSERT_EQ(collect(&reader).size(), 1u);
	EXPECT_EQ(reader.state(), Reader::Error);
	EXPECT_EQ(pos, invalid.size());
	size_t calls_at_error = calls;
	reader.hold();
	reader.input("</root>");
	for (int i = 0; i < 3; ++i) {
		EXPECT_FALSE(reader.next());
		EXPECT_EQ(reader.state(), Reader::Error);
		EXPECT_FALSE(reader.is_not_enough_input());
	}
	EXPECT_EQ(calls, calls_at_error);
	reader.parse([] {});
	reader.input("<fresh/>");
	EXPECT_EQ(collect(&reader).size(), 2u);
	EXPECT_NE(reader.state(), Reader::Error);
	EXPECT_EQ(reader.depth(), 1);
}

TEST(EndTags, NestedErrorDoesNotBecomeAHeldEndEvent)
{
	Reader reader;
	reader.parse([] {});
	reader.input("<root><branch></root>");
	ASSERT_TRUE(reader.next());
	ASSERT_TRUE(reader.next());
	reader.nest();
	EXPECT_FALSE(reader.next());
	EXPECT_EQ(reader.state(), Reader::Error);
	EXPECT_FALSE(reader.next());
	EXPECT_EQ(reader.state(), Reader::Error);
	EXPECT_EQ(reader.path(), "/root/branch");
	EXPECT_EQ(reader.depth(), 3);
}
