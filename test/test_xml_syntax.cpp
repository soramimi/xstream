#include "test.h"
#include <gtest/gtest.h>
#include <tuple>

namespace {

using Reader = xstream::Reader;
using Result = Reader::ReadResult;
using Code = Reader::ErrorCode;
using Event = std::tuple<Reader::StateType, std::string, std::string,
	std::vector<std::pair<std::string, std::string>>, std::string>;

struct Outcome {
	std::vector<Event> events;
	Result result;
	Code code;
	size_t offset;
};

Outcome drain(Reader *reader)
{
	Outcome out;
	while (reader->next()) {
		if (out.events.size() >= 1000) {
			ADD_FAILURE() << "Reader did not stop";
			break;
		}
		std::vector<std::pair<std::string, std::string>> attributes;
		for (auto const &a : reader->attributes()) attributes.emplace_back(a.first, a.second.to_string());
		out.events.emplace_back(reader->state(), reader->path(), reader->name(), attributes,
			reader->is_end_element() ? reader->text() : std::string());
	}
	out.result = reader->result();
	out.code = reader->error().code;
	out.offset = reader->error().offset;
	return out;
}

void expect_same(Outcome const &actual, Outcome const &expected)
{
	EXPECT_EQ(actual.events, expected.events);
	EXPECT_EQ(actual.result, expected.result);
	EXPECT_EQ(actual.code, expected.code);
	EXPECT_EQ(actual.offset, expected.offset);
}

void check_splits(std::string const &xml, Outcome const &expected)
{
	for (size_t split = 0; split <= xml.size(); ++split) {
		SCOPED_TRACE(split);
		Reader reader;
		reader.input(std::string_view(xml).substr(0, split));
		auto out = drain(&reader);
		reader.input(std::string_view(xml).substr(split));
		reader.finish_input();
		auto rest = drain(&reader);
		out.events.insert(out.events.end(), rest.events.begin(), rest.events.end());
		out.result = rest.result;
		out.code = rest.code;
		out.offset = rest.offset;
		expect_same(out, expected);
	}
	Reader reader;
	size_t position = 0;
	reader.parse([&] {
		if (position < xml.size()) reader.input(std::string_view(xml).substr(position++, 1));
		else reader.finish_input();
	});
	expect_same(drain(&reader), expected);
}

} // namespace

TEST(XmlSyntax, DuplicateAttributesAreRejectedBeforePublishingTheTag)
{
	std::vector<std::pair<std::string, std::string>> cases = {
		{"<r a='1' a='2'/>", "a='2'"},
		{"<r a='1' a='1'></r>", "a='1'></r>"},
		{"<r z='1' b='2' a='3' b='4'/>", "b='4'"},
		{"<r z='1' z='2' a='3' a='4'/>", "z='2'"},
		{"<r xmlns:p='one' xmlns:p='two'/>", "xmlns:p='two'"},
		{"<r p:a='one' p:a='two'/>", "p:a='two'"},
		{"<r ab='1' a='2' ab='3'/>", "ab='3'"},
		{"<?xml version='1.0' version='1.1'?><r/>", "version='1.1'"},
		{"<?custom a='1' b='2' a='3'?><r/>", "a='3'"}
	};
	for (auto const &test : cases) {
		SCOPED_TRACE(test.first);
		Reader reader(test.first);
		auto out = drain(&reader);
		EXPECT_EQ(out.result, Result::Error);
		EXPECT_EQ(out.code, Code::InvalidSyntax);
		EXPECT_EQ(out.offset, test.first.find(test.second));
		EXPECT_FALSE(reader.error().message.empty());
		EXPECT_TRUE(out.events.empty());
		EXPECT_EQ(reader.path(), "");
		EXPECT_TRUE(reader.attributes().empty());
		EXPECT_EQ(reader.statistics().retained_attribute_bytes, 0u);
		check_splits(test.first, out);
	}
}

TEST(XmlSyntax, UniqueAttributesPreserveOrderAndRemainCaseSensitive)
{
	std::string xml = "<r z='1' A='2' a='3' p:a='4' q:a='5' ab='6'>"
		"<x a='child'/><x a='sibling'/></r>";
	Reader reader(xml);
	auto out = drain(&reader);
	ASSERT_EQ(out.result, Result::EndDocument);
	ASSERT_EQ(out.events.size(), 6u);
	EXPECT_EQ(std::get<3>(out.events.front()),
		(std::vector<std::pair<std::string, std::string>>{
			{"z", "1"}, {"A", "2"}, {"a", "3"}, {"p:a", "4"}, {"q:a", "5"}, {"ab", "6"}}));
	check_splits(xml, out);
}

TEST(XmlSyntax, InvalidChildDoesNotPublishAttributesOrChangeParentState)
{
	std::string xml = "<r keep='parent'><x a='1' a='2'/></r>";
	Reader reader(xml);
	auto out = drain(&reader);
	EXPECT_EQ(out.code, Code::InvalidSyntax);
	EXPECT_EQ(out.offset, xml.find("a='2'"));
	ASSERT_EQ(out.events.size(), 1u);
	EXPECT_EQ(reader.path(), "/r");
	EXPECT_EQ(reader.name(), "r");
	EXPECT_EQ(reader.attribute("keep", {}), "parent");
	EXPECT_FALSE(reader.attribute("a"));
	check_splits(xml, out);
}

TEST(XmlSyntax, DuplicateCheckWaitsForTheCompleteTag)
{
	Reader reader;
	reader.input("<r a='1' a='2'");
	EXPECT_EQ(reader.advance(), Result::NeedInput);
	EXPECT_EQ(reader.state(), Reader::None);
	EXPECT_TRUE(reader.path().empty());
	reader.input(">");
	EXPECT_EQ(reader.advance(), Result::Error);
	EXPECT_EQ(reader.error().code, Code::InvalidSyntax);
	EXPECT_EQ(reader.error().offset, 9u);
}

TEST(XmlSyntax, DuplicateCheckWorksWhenNamesWrapInTheInputRing)
{
	std::string xml = "<r>";
	for (int i = 0; i < 40; ++i) xml += "<x/>";
	xml += "<x abcdefghijkl='1' abcdefghijkl='2'/></r>";
	Reader borrowed(xml);
	auto expected = drain(&borrowed);
	EXPECT_EQ(expected.code, Code::InvalidSyntax);
	EXPECT_EQ(expected.offset, xml.find("abcdefghijkl='2'"));
	Reader::Limits limits;
	limits.max_buffer_bytes = 64;
	Reader streamed(limits);
	size_t position = 0;
	streamed.parse([&] {
		if (position < xml.size()) streamed.input(std::string_view(xml).substr(position++, 1));
		else streamed.finish_input();
	});
	expect_same(drain(&streamed), expected);
	EXPECT_LE(streamed.statistics().buffer_capacity, 64u);
}

TEST(XmlSyntax, RejectsUnsupportedMarkupDeclarations)
{
	for (std::string const declaration : {"<!DOCTYPE r>",
		"<!DOCTYPE r SYSTEM 'urn:external'>",
		"<!DOCTYPE r PUBLIC 'identifier' 'urn:external'>",
		"<!DOCTYPE r [<!ENTITY x 'value'>]>",
		"<!ENTITY x 'value'>", "<!ENTITY x SYSTEM 'urn:external'>",
		"<!ENTITY % x SYSTEM 'urn:external'>", "<!ELEMENT r ANY>",
		"<!ATTLIST r a CDATA #IMPLIED>", "<!NOTATION n SYSTEM 'urn:external'>"}) {
		for (std::string const prefix : {"", "<r>", "<r/>"}) {
			std::string xml = prefix + declaration;
			SCOPED_TRACE(xml);
			Reader reader(xml);
			auto out = drain(&reader);
			EXPECT_EQ(out.result, Result::Error);
			EXPECT_EQ(out.code, Code::UnsupportedDeclaration);
			EXPECT_EQ(out.offset, prefix.size());
			EXPECT_FALSE(reader.error().message.empty());
			EXPECT_EQ(out.events.size(), prefix.empty() ? 0u : (prefix == "<r>" ? 1u : 2u));
			check_splits(xml, out);
		}
	}
}

TEST(XmlSyntax, DeclarationPrefixesDistinguishWaitingEofAndUnsupportedInput)
{
	for (std::string const xml : {"<!", "<!-", "<![", "<![CDATA"}) {
		Reader reader;
		reader.input(xml);
		EXPECT_EQ(reader.advance(), Result::NeedInput);
		EXPECT_EQ(reader.error().code, Code::None);
		reader.finish_input();
		EXPECT_EQ(reader.advance(), Result::Error);
		EXPECT_EQ(reader.error().code, Code::UnexpectedEof);
		EXPECT_EQ(reader.error().offset, xml.size());
	}
	for (bool allow : {false, true}) {
		Reader reader;
		reader.set_allow_invalid_comments(allow);
		reader.input("<!");
		EXPECT_EQ(reader.advance(), Result::NeedInput);
		reader.input("D");
		EXPECT_EQ(reader.advance(), Result::Error);
		EXPECT_EQ(reader.error().code, Code::UnsupportedDeclaration);
		EXPECT_EQ(reader.error().offset, 0u);
	}
}

TEST(XmlSyntax, SupportedDeclarationsCommentsAndCdataStillWork)
{
	std::string xml = "<?xml version='1.0' encoding='UTF-8'?>"
		"<?xml-stylesheet type='text/xsl' href='style.xsl'?>"
		"<r><?custom a='value'?><!-- <!DOCTYPE r> -->"
		"<![CDATA[<!ENTITY x 'text'>]]></r>";
	Reader reader(xml);
	auto out = drain(&reader);
	ASSERT_EQ(out.result, Result::EndDocument);
	ASSERT_EQ(out.events.size(), 7u);
	EXPECT_EQ(std::get<0>(out.events[0]), Reader::Declaration);
	EXPECT_EQ(std::get<1>(out.events[0]), "/?xml");
	EXPECT_EQ(std::get<1>(out.events[1]), "/?xml-stylesheet");
	EXPECT_EQ(std::get<1>(out.events[3]), "/r/?custom");
	EXPECT_EQ(std::get<0>(out.events[4]), Reader::Comment);
	EXPECT_EQ(std::get<4>(out.events.back()), "<!ENTITY x 'text'>");
	check_splits(xml, out);
}

TEST(XmlSyntax, SyntaxErrorsStopCallbacksAndPreserveTheirFirstDiagnostic)
{
	for (std::string const xml : {"<r a='1' a='2'/>", "<!DOCTYPE r>"}) {
		Reader reader;
		int calls = 0;
		reader.parse([&] { ++calls; });
		reader.input(xml);
		auto out = drain(&reader);
		EXPECT_EQ(out.result, Result::Error);
		EXPECT_EQ(calls, 0);
		reader.input("<extra/>");
		reader.finish_input();
		reader.input_error("later I/O error");
		EXPECT_EQ(reader.advance(), Result::Error);
		EXPECT_EQ(reader.error().code, out.code);
		EXPECT_EQ(reader.error().offset, out.offset);
		EXPECT_EQ(calls, 0);
		reader.parse({});
		reader.input("<r a='ok'/>");
		reader.finish_input();
		EXPECT_EQ(drain(&reader).result, Result::EndDocument);
	}
}
