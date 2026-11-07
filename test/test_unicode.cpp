#include "test.h"
#include <gtest/gtest.h>
#include <random>
#include <tuple>

namespace {

using Reader = xstream::Reader;
using Code = Reader::ErrorCode;
using Result = Reader::ReadResult;

// u8 literals contain char in C++17 and char8_t in C++20; copy their UTF-8 bytes.
template<class Char, size_t N>
std::string utf8_bytes(Char const (&literal)[N])
{
	static_assert(sizeof(Char) == 1, "Expected a UTF-8 literal");
	return std::string(reinterpret_cast<char const *>(literal), N - 1);
}
using Event = std::tuple<Reader::StateType, std::string, std::string,
	std::vector<std::pair<std::string, std::string>>, std::string>;

struct Outcome {
	std::vector<Event> events;
	Result result;
	Code code;
	size_t offset;
};

Outcome drain(Reader *reader, bool access_values = true)
{
	Outcome out;
	while (reader->next()) {
		if (out.events.size() >= 1000) { ADD_FAILURE() << "Reader did not stop"; break; }
		std::vector<std::pair<std::string, std::string>> attributes;
		std::string text;
		if (access_values) {
			for (auto const &a : reader->attributes()) attributes.emplace_back(a.first, a.second.to_string());
			if (reader->is_end_element()) text = reader->text();
			else if (reader->is_characters()) {
				auto part = reader->characters().decode();
				text.assign(part.begin(), part.end());
			}
		}
		out.events.emplace_back(reader->state(), reader->name(), reader->path(), attributes, text);
	}
	out.result = reader->result();
	out.code = reader->error().code;
	out.offset = reader->error().offset;
	return out;
}

Outcome stream(std::string const &xml, std::vector<size_t> const &chunks)
{
	Reader reader;
	size_t position = 0, chunk = 0;
	reader.parse([&] {
		if (chunk < chunks.size()) {
			size_t length = chunks[chunk++];
			reader.input(std::string_view(xml).substr(position, length));
			position += length;
			if (chunk == chunks.size()) reader.finish_input();
		} else reader.finish_input();
	});
	return drain(&reader);
}

void expect_same(Outcome const &a, Outcome const &b)
{
	EXPECT_EQ(a.events, b.events);
	EXPECT_EQ(a.result, b.result);
	EXPECT_EQ(a.code, b.code);
	EXPECT_EQ(a.offset, b.offset);
}

void check_splits(std::string const &xml, Outcome const &expected)
{
	for (size_t split = 1; split < xml.size(); ++split) {
		SCOPED_TRACE(split);
		expect_same(stream(xml, {split, xml.size() - split}), expected);
	}
	expect_same(stream(xml, std::vector<size_t>(xml.size(), 1)), expected);
}

Outcome check_error(std::string const &xml, Code code, size_t offset)
{
	Reader reader(xml);
	auto out = drain(&reader);
	EXPECT_EQ(out.result, Result::Error);
	EXPECT_EQ(out.code, code);
	EXPECT_EQ(out.offset, offset);
	EXPECT_FALSE(reader.error().message.empty());
	Reader without_accessors(xml);
	auto lazy = drain(&without_accessors, false);
	EXPECT_EQ(lazy.result, Result::Error);
	EXPECT_EQ(lazy.code, out.code);
	EXPECT_EQ(lazy.offset, out.offset);
	check_splits(xml, out);
	return out;
}

std::string utf8(unsigned int c)
{
	if (c < 0x80) return std::string(1, char(c));
	if (c < 0x800) return {char(0xc0 + c / 64), char(0x80 + c % 64)};
	if (c < 0x10000) return {char(0xe0 + c / 4096), char(0x80 + c / 64 % 64), char(0x80 + c % 64)};
	return {char(0xf0 + c / 262144), char(0x80 + c / 4096 % 64), char(0x80 + c / 64 % 64), char(0x80 + c % 64)};
}

} // namespace

TEST(Unicode, ReferencesDecodeToUtf8InTextAndAttributes)
{
	std::string refs = "&amp;&lt;&gt;&quot;&apos;&#65;&#x41;&#128512;&#x1F600;";
	std::string value = utf8_bytes(u8"&<>\"'AA😀😀");
	std::string xml = "<r a='" + refs + "'>" + refs + "</r>";
	Reader reader(xml);
	auto out = drain(&reader);
	ASSERT_EQ(out.result, Result::EndDocument);
	ASSERT_EQ(out.events.size(), 3u);
	EXPECT_EQ(std::get<3>(out.events[0]), (std::vector<std::pair<std::string, std::string>>{{"a", value}}));
	EXPECT_EQ(std::get<4>(out.events[1]), value);
	EXPECT_EQ(std::get<4>(out.events[2]), value);
	check_splits(xml, out);
	EXPECT_EQ(xstream::html_decode(refs), value);
	EXPECT_EQ(xstream::html_decode("&amp;lt;&#38;#65;"), "&lt;&#65;"); // Decode once.
}

TEST(Unicode, LegalXmlCharacterBoundariesWorkAsLiteralsAndReferences)
{
	for (unsigned int c : {9u, 10u, 13u, 0x20u, 0x7fu, 0x85u, 0x80u, 0x7ffu,
		0x800u, 0xd7ffu, 0xe000u, 0xfdd0u, 0xfffdu, 0x10000u, 0x1ffffu, 0x10ffffu}) {
		SCOPED_TRACE(c);
		std::string expected = utf8(c);
		for (std::string const &text : {expected, "&#" + std::to_string(c) + ";"}) {
			std::string xml = "<r>" + text + "</r>";
			Reader reader(xml);
			auto out = drain(&reader);
			ASSERT_EQ(out.result, Result::EndDocument);
			ASSERT_EQ(out.events.size(), 3u);
			EXPECT_EQ(std::get<4>(out.events.back()), expected);
			check_splits(xml, out);
		}
	}
}

TEST(Unicode, InvalidAndUnknownReferencesAreErrorsWithoutCallingAccessors)
{
	for (std::string const ref : {"&;", "&unknown;", "&nbsp;", "&AMP;", "&am;", "&#;", "&#x;",
		"&#X41;", "&#-1;", "&#+65;", "&# 65;", "&#65x;", "&#x41z;", "&#x-1;", "&#x+41;",
		"&#0;", "&#1;", "&#11;", "&#xD800;", "&#xDFFF;", "&#xFFFE;", "&#65535;",
		"&#1114112;", "&#x110000;", "&#4294967361;", "&amp&;", "&amp extra;"}) {
		SCOPED_TRACE(ref);
		for (std::string const prefix : {"<r>", "<r a='"}) {
			std::string xml = prefix + ref + (prefix == "<r>" ? "</r>" : "'/>");
			auto out = check_error(xml, Code::InvalidCharacterReference, prefix.size());
			EXPECT_EQ(out.events.size(), prefix == "<r>" ? 1u : 0u);
		}
		std::string output;
		EXPECT_FALSE(xstream::html_decode(ref, &output));
	}
}

TEST(Unicode, IncompleteReferencesWaitUntilCompletedBoundaryOrEof)
{
	for (std::string const ref : {"&", "&am", "&#", "&#x", "&#12", "&#x1F6"}) {
		for (std::string const prefix : {"<r>", "<r a='"}) {
			Reader reader;
			reader.input(prefix + ref);
			drain(&reader);
			EXPECT_EQ(reader.result(), Result::NeedInput);
			EXPECT_EQ(reader.error().code, Code::None);
			reader.finish_input();
			EXPECT_EQ(reader.advance(), Result::Error);
			EXPECT_EQ(reader.error().code, Code::InvalidCharacterReference);
			EXPECT_EQ(reader.error().offset, prefix.size());
			check_error(prefix + ref, Code::InvalidCharacterReference, prefix.size());
			check_error(prefix + ref + (prefix == "<r>" ? "</r>" : "'/>") , Code::InvalidCharacterReference, prefix.size());
		}
	}
	Reader reader;
	reader.input("<r a='&#x1F6");
	EXPECT_EQ(reader.advance(), Result::NeedInput);
	reader.input("00;'>x&amp");
	ASSERT_TRUE(reader.next());
	EXPECT_EQ(reader.attribute("a", {}), utf8_bytes(u8"😀"));
	EXPECT_EQ(reader.advance(), Result::NeedInput);
	reader.input(";</r>");
	reader.finish_input();
	EXPECT_EQ(drain(&reader).result, Result::EndDocument);
}

TEST(Unicode, ReferenceSyntaxIsLiteralInCommentsAndCdata)
{
	std::string xml = "<r><!-- &bad; &#0; & --><![CDATA[&bad; &#0; &]]></r>";
	Reader reader(xml);
	auto out = drain(&reader);
	ASSERT_EQ(out.result, Result::EndDocument);
	ASSERT_EQ(out.events.size(), 4u);
	EXPECT_EQ(std::get<4>(out.events.back()), "&bad; &#0; &");
	check_splits(xml, out);
}

TEST(Unicode, MalformedUtf8IsRejectedInEveryParsingContext)
{
	for (std::string const bytes : {"\x80", "\xbf", "\xc0\x80", "\xc1\xbf", "\xf5\x80\x80\x80",
		"\xff", "\xe0\x80\x80", "\xed\xa0\x80", "\xf0\x80\x80\x80", "\xf4\x90\x80\x80",
		"\xe2(\xa1", "\xc2\x7f", "\xe2\x82x", "\xf0\x9f\x98"}) {
		for (std::string const prefix : {"<r>", "<r a='", "<r><!--", "<r><![CDATA[", "<", "<r ", "<r></", "<?p a='"}) {
			check_error(prefix + bytes + "</r>", Code::InvalidUtf8, prefix.size());
		}
	}
}

TEST(Unicode, ForbiddenXmlCharactersAreRejectedInEveryParsingContext)
{
	for (unsigned int c : {0u, 1u, 8u, 11u, 12u, 14u, 31u, 0xfffeu, 0xffffu}) {
		SCOPED_TRACE(c);
		for (std::string const prefix : {"<r>", "<r a='", "<r><!--", "<r><![CDATA[", "<", "<r ", "<?p a='"}) {
			check_error(prefix + utf8(c) + "</r>", Code::InvalidCharacter, prefix.size());
		}
	}
}

TEST(Unicode, PartialUtf8WaitsAndEofReportsTheSequenceStart)
{
	std::string face = utf8_bytes(u8"😀");
	for (size_t count = 1; count < face.size(); ++count) {
		std::string xml = "<r>" + face.substr(0, count);
		Reader reader;
		reader.input(xml);
		drain(&reader);
		EXPECT_EQ(reader.result(), Result::NeedInput);
		auto steps = reader.statistics().scan_steps;
		for (int i = 0; i < 10; ++i) EXPECT_EQ(reader.advance(), Result::NeedInput);
		EXPECT_EQ(reader.statistics().scan_steps, steps);
		reader.input(face.substr(count) + "</r>");
		reader.finish_input();
		auto out = drain(&reader);
		ASSERT_EQ(out.result, Result::EndDocument);
		ASSERT_EQ(out.events.size(), 2u);
		EXPECT_EQ(std::get<4>(out.events.back()), face);
		check_error(xml, Code::InvalidUtf8, 3);
	}
}

TEST(Unicode, ElementAndAttributeNamesUseXmlNameRanges)
{
	std::string xml = utf8_bytes(u8"<日本😀 属性='値' á='combining' a·='middle'><é/></日本😀>");
	Reader reader(xml);
	auto out = drain(&reader);
	ASSERT_EQ(out.result, Result::EndDocument);
	ASSERT_EQ(out.events.size(), 4u);
	EXPECT_EQ(std::get<1>(out.events[0]), utf8_bytes(u8"日本😀"));
	EXPECT_EQ(std::get<2>(out.events[1]), utf8_bytes(u8"/日本😀/é"));
	check_splits(xml, out);
	for (unsigned int c : {0xc0u, 0xd6u, 0xd8u, 0xf6u, 0xf8u, 0x2ffu, 0x370u, 0x37du,
		0x37fu, 0x1fffu, 0x200cu, 0x200du, 0x2070u, 0x218fu, 0x2c00u, 0x2fefu,
		0x3001u, 0xd7ffu, 0xf900u, 0xfdcfu, 0xfdf0u, 0xfffdu, 0x10000u, 0xeffffu}) {
		SCOPED_TRACE(c);
		std::string name = utf8(c);
		std::string sample = "<" + name + " " + name + "='ok'></" + name + ">";
		Reader boundary(sample);
		auto expected = drain(&boundary);
		ASSERT_EQ(expected.result, Result::EndDocument);
		check_splits(sample, expected);
	}
}

TEST(Unicode, InvalidNamesAndContinuationOnlyCharactersAreDistinguished)
{
	for (unsigned int c : {0x30u, 0x2du, 0x2eu, 0xb7u, 0x300u, 0x36fu, 0x203fu, 0x2040u}) {
		SCOPED_TRACE(c);
		std::string name = utf8(c);
		check_error("<" + name + "/>", Code::InvalidSyntax, 1);
		check_error("<r " + name + "='x'/>", Code::InvalidSyntax, 3);
		std::string xml = "<a" + name + " a" + name + "='ok'/>";
		Reader reader(xml);
		auto out = drain(&reader);
		ASSERT_EQ(out.result, Result::EndDocument);
		check_splits(xml, out);
	}
	for (unsigned int c : {0xd7u, 0xf7u, 0x37eu, 0x200bu, 0x200eu, 0x206fu,
		0x2190u, 0x2bffu, 0x2ff0u, 0x3000u, 0xe000u, 0xf8ffu, 0xfdd0u, 0xfdefu, 0xf0000u}) {
		SCOPED_TRACE(c);
		check_error("<" + utf8(c) + "/>", Code::InvalidSyntax, 1);
		check_error("<a" + utf8(c) + "/>", Code::InvalidSyntax, 2);
		check_error("<r a" + utf8(c) + "='x'/>", Code::InvalidSyntax, 4);
	}
}

TEST(Unicode, UnicodeNamesAreComparedExactlyWithoutNormalization)
{
	check_error(utf8_bytes(u8"<é></é>"), Code::MismatchedEndTag, 4);
	std::string duplicate = utf8_bytes(u8"<r é='one' é='two'/>");
	check_error(duplicate, Code::InvalidSyntax, duplicate.find(utf8_bytes(u8"é='two'")));
	std::string xml = utf8_bytes(u8"<r é='one' é='two'/>");
	Reader reader(xml);
	EXPECT_EQ(drain(&reader).result, Result::EndDocument);
}

TEST(Unicode, XmlEncodingDeclarationAcceptsOnlyUtf8)
{
	for (std::string const encoding : {"UTF-8", "utf-8", "uTf-8"}) {
		std::string xml = "<?xml version='1.0' encoding='" + encoding + "'?><r/>";
		Reader reader(xml);
		auto out = drain(&reader);
		ASSERT_EQ(out.result, Result::EndDocument);
		check_splits(xml, out);
	}
	for (std::string const encoding : {"UTF-16", "UTF-16LE", "UTF-32", "Shift_JIS", "ISO-8859-1", "UTF8"}) {
		std::string prefix = "<?xml version='1.0' encoding='";
		auto out = check_error(prefix + encoding + "'?><r/>", Code::UnsupportedEncoding, prefix.size());
		EXPECT_TRUE(out.events.empty());
	}
	for (std::string const encoding : {"", "8UTF", "UTF 8", "UTF&#45;8", "UTF/8"}) {
		std::string prefix = "<?xml version='1.0' encoding='";
		check_error(prefix + encoding + "'?><r/>", Code::InvalidSyntax, prefix.size());
	}
	Reader pi("<?xml-stylesheet encoding='Shift_JIS'?><r/>");
	EXPECT_EQ(drain(&pi).result, Result::EndDocument);
}

TEST(Unicode, Utf8BomIsSkippedOnlyAtTheStartAndUtf16BomIsRejected)
{
	std::string bom = "\xef\xbb\xbf";
	std::string xml = bom + utf8_bytes(u8"<?xml version='1.0' encoding='UTF-8'?><r>値</r>");
	Reader reader(xml);
	auto out = drain(&reader);
	ASSERT_EQ(out.result, Result::EndDocument);
	ASSERT_EQ(out.events.size(), 4u);
	EXPECT_EQ(std::get<0>(out.events.front()), Reader::Declaration);
	EXPECT_EQ(std::get<4>(out.events.back()), utf8_bytes(u8"値"));
	check_splits(xml, out);
	check_error(bom + bom + "<r/>", Code::InvalidSyntax, 3);
	check_error(bom + "<r>&bad;</r>", Code::InvalidCharacterReference, 6);
	check_error(std::string("\xff\xfe", 2) + "<r/>", Code::UnsupportedEncoding, 0);
	check_error(std::string("\xfe\xff", 2) + "<r/>", Code::UnsupportedEncoding, 0);
}

TEST(Unicode, EveryEofAndRandomChunkingMatchesOneShotParsing)
{
	std::string xml = utf8_bytes(u8"<?xml version='1.0'?><日本 属性='値&#x1F600;'>é😀&amp;"
		u8"<![CDATA[😀&raw]]><!--値--><子/></日本>");
	for (size_t length = 0; length <= xml.size(); ++length) {
		SCOPED_TRACE(length);
		std::string prefix = xml.substr(0, length);
		Reader reader(prefix);
		expect_same(stream(prefix, std::vector<size_t>(prefix.size(), 1)), drain(&reader));
	}
	std::mt19937 random(20261107);
	for (std::string const &sample : std::vector<std::string>{xml, utf8_bytes(u8"<日本>値&bad;</日本>"), std::string("<r>\xf0\x9f\x98"),
		std::string("<?xml encoding='UTF-16'?><r/>"), utf8_bytes(u8"<r a='😀&#0;'/>"), utf8_bytes(u8"<r>é\x01</r>")}) {
		Reader reader(sample);
		auto expected = drain(&reader);
		for (int trial = 0; trial < 40; ++trial) {
			std::vector<size_t> chunks;
			size_t remaining = sample.size();
			while (remaining) {
				size_t size = std::min(remaining, size_t(1 + random() % 11));
				chunks.push_back(size);
				remaining -= size;
			}
			expect_same(stream(sample, chunks), expected);
		}
	}
}

TEST(Unicode, ErrorsKeepTheirDiagnosticAndDoNotRequestAdditionalInput)
{
	for (std::string const xml : {"<r>&bad;</r>", "<r>\x80</r>", "<r>\x01</r>", "<?xml encoding='UTF-16'?><r/>"}) {
		Reader reader;
		int calls = 0;
		reader.parse([&] { ++calls; });
		reader.input(xml);
		auto out = drain(&reader, false);
		EXPECT_EQ(out.result, Result::Error);
		std::string message = reader.error().message;
		reader.input("<r/>");
		reader.finish_input();
		reader.input_error("later error");
		EXPECT_EQ(reader.advance(), Result::Error);
		EXPECT_EQ(reader.error().code, out.code);
		EXPECT_EQ(reader.error().offset, out.offset);
		EXPECT_EQ(reader.error().message, message);
		EXPECT_EQ(calls, 0);
		reader.parse({});
		reader.input(utf8_bytes(u8"<r>😀&#65;</r>"));
		reader.finish_input();
		EXPECT_EQ(drain(&reader).result, Result::EndDocument);
	}
}

TEST(Unicode, RingWrapAndReaderCopyPreserveValidationState)
{
	std::string xml = "<r>";
	for (int i = 0; i < 40; ++i) xml += "<x/>";
	xml += utf8_bytes(u8"<名 a='値&#x1F600;'>é&bad;</名></r>");
	Reader borrowed(xml);
	auto expected = drain(&borrowed);
	EXPECT_EQ(expected.code, Code::InvalidCharacterReference);
	EXPECT_EQ(expected.offset, xml.find("&bad;"));
	Reader::Limits limits;
	limits.max_buffer_bytes = 64;
	Reader reader(limits);
	size_t position = 0;
	reader.parse([&] {
		if (position < xml.size()) reader.input(std::string_view(xml).substr(position++, 1));
		else reader.finish_input();
	});
	expect_same(drain(&reader), expected);
	EXPECT_LE(reader.statistics().buffer_capacity, 64u);

	Reader original;
	original.input("<r a='&#x1F6");
	ASSERT_EQ(original.advance(), Result::NeedInput);
	Reader copy = original;
	original.input("00;'/>");
	original.finish_input();
	ASSERT_TRUE(original.next());
	EXPECT_EQ(original.attribute("a", {}), utf8_bytes(u8"😀"));
	copy.input("42;'/>");
	copy.finish_input();
	ASSERT_TRUE(copy.next());
	EXPECT_EQ(copy.attribute("a", {}), utf8_bytes(u8"🙂"));
	EXPECT_EQ(drain(&copy).result, Result::EndDocument);
	EXPECT_EQ(drain(&original).result, Result::EndDocument);
}

TEST(Unicode, ByteLimitsApplyToCompleteMultibyteCharacters)
{
	struct Case { size_t Reader::Limits::*limit; size_t size; std::string xml; };
	for (auto const &test : std::vector<Case>{
		{&Reader::Limits::max_name_bytes, 3, utf8_bytes(u8"<名/>")},
		{&Reader::Limits::max_name_bytes, 3, utf8_bytes(u8"<r 名='v'/>")},
		{&Reader::Limits::max_path_bytes, 4, utf8_bytes(u8"<名/>")},
		{&Reader::Limits::max_token_bytes, 8, utf8_bytes(u8"<r>😀😀</r>")},
		{&Reader::Limits::max_attribute_value_bytes, 4, utf8_bytes(u8"<r a='😀'/>")},
		{&Reader::Limits::max_element_text_bytes, 4, utf8_bytes(u8"<r>😀</r>")},
		{&Reader::Limits::max_total_attribute_bytes, 5, utf8_bytes(u8"<r a='😀'/>")}}) {
		for (bool enough : {false, true}) {
			Reader::Limits limits;
			limits.*test.limit = test.size - (enough ? 0 : 1);
			Reader reader(test.xml, limits);
			auto out = drain(&reader);
			EXPECT_EQ(out.result, enough ? Result::EndDocument : Result::Error);
			EXPECT_EQ(out.code, enough ? Code::None : Code::LimitExceeded);
		}
	}
}

TEST(Unicode, LongReferencesAndMultibyteTextUseLinearScanningWork)
{
	std::string multibyte;
	for (int i = 0; i < 2500; ++i) multibyte += utf8_bytes(u8"😀");
	for (std::string const &text : {"&#" + std::string(10000, '0') + "65;",
		"&" + std::string(10000, 'x') + ";", std::string(10000, '9'), multibyte}) {
		std::string xml = "<r>" + text + "</r>";
		Reader reader;
		size_t position = 0;
		reader.parse([&] {
			if (position < xml.size()) reader.input(std::string_view(xml).substr(position++, 1));
			else reader.finish_input();
		});
		auto out = drain(&reader);
		EXPECT_EQ(out.code, text[0] == '&' && text[1] == 'x' ? Code::InvalidCharacterReference : Code::None);
		EXPECT_LE(reader.statistics().scan_steps, xml.size() * 8 + 64);
		EXPECT_LE(reader.statistics().copied_input_bytes, xml.size() * 3 + 256);
	}
}

TEST(Unicode, SharedEscapingAndWriterPreserveUnicodeAndSpecialCharacters)
{
	std::string value = utf8_bytes(u8"é日本😀 &<>\"'\t\n\r");
	EXPECT_EQ(xstream::html_decode(xstream::html_encode(value)), value);
	EXPECT_EQ(xstream::html_encode(utf8_bytes(u8"é😀"), false), "&#233;&#128512;");
	EXPECT_EQ(xstream::html_decode(xstream::html_encode(value, false)), value);
	for (std::string const &invalid : {std::string(1, '\0'), std::string("\xc0\x80"), std::string("\x01")}) {
		std::string output;
		EXPECT_FALSE(xstream::html_encode(invalid, &output));
		EXPECT_FALSE(xstream::html_decode(invalid, &output));
	}
	std::string xml;
	xstream::Writer writer([&](char const *p, int n) { xml.append(p, size_t(n)); return n; });
	writer.start_document();
	writer.start_element("r");
	writer.write_attribute("a", value);
	writer.write_characters(value);
	writer.end_document();
	EXPECT_NE(xml.find("&amp;&lt;&gt;&quot;&apos;"), std::string::npos);
	Reader reader(xml);
	auto out = drain(&reader);
	ASSERT_EQ(out.result, Result::EndDocument);
	bool found = false;
	for (auto const &event : out.events) if (std::get<0>(event) == Reader::EndElement) {
		found = true;
		EXPECT_EQ(std::get<4>(event), value);
		EXPECT_EQ(std::get<3>(event), (std::vector<std::pair<std::string, std::string>>{{"a", value}}));
	}
	EXPECT_TRUE(found);
}
