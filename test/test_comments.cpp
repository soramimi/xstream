#include "test.h"
#include <gtest/gtest.h>
#include <tuple>

namespace {

using Reader = xstream::Reader;
using Result = Reader::ReadResult;
using Code = Reader::ErrorCode;
using Event = std::tuple<Reader::StateType, std::string, std::string>;

struct Outcome {
	std::vector<Event> events;
	Result result;
	Code code;
	size_t offset;
};

Outcome drain(Reader *reader)
{
	Outcome out;
	size_t steps = 0;
	while (reader->next()) {
		if (++steps >= 100) {
			ADD_FAILURE() << "Reader did not stop";
			break;
		}
		if (reader->result() == Result::NeedInput) {
			EXPECT_EQ(reader->state(), Reader::None);
			EXPECT_FALSE(reader->is_not_enough_input());
			continue;
		}
		EXPECT_EQ(reader->result(), Result::Event);
		out.events.emplace_back(reader->state(), reader->path(),
			reader->is_end_element() ? reader->text() : std::string());
		if (reader->state() == Reader::Comment) {
			EXPECT_TRUE(reader->characters().decode().empty());
		}
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

void check_splits(std::string const &xml, bool allow, Outcome const &expected)
{
	for (size_t split = 0; split <= xml.size(); ++split) {
		SCOPED_TRACE(split);
		Reader reader;
		reader.set_allow_invalid_comments(allow);
		reader.input(std::string_view(xml).substr(0, split));
		auto actual = drain(&reader);
		reader.input(std::string_view(xml).substr(split));
		reader.finish_input();
		auto rest = drain(&reader);
		actual.events.insert(actual.events.end(), rest.events.begin(), rest.events.end());
		actual.result = rest.result;
		actual.code = rest.code;
		actual.offset = rest.offset;
		expect_same(actual, expected);
	}
	Reader reader;
	reader.set_allow_invalid_comments(allow);
	size_t position = 0;
	reader.parse([&] {
		if (position < xml.size()) reader.input(std::string_view(xml).substr(position++, 1));
		else reader.finish_input();
	});
	expect_same(drain(&reader), expected);
}

} // namespace

TEST(Comments, ValidBodiesAreAcceptedInBothModes)
{
	for (bool allow : {false, true}) {
		for (std::string const body : {"", "abc", "a-b", "a- ", "a>b", "<x/>", "a- -b"}) {
			SCOPED_TRACE(body);
			std::string xml = "<r><!--" + body + "--></r>";
			Reader reader(xml);
			EXPECT_FALSE(reader.allow_invalid_comments());
			reader.set_allow_invalid_comments(allow);
			auto out = drain(&reader);
			EXPECT_EQ(out.result, Result::EndDocument);
			EXPECT_EQ(out.code, Code::None);
			EXPECT_EQ(out.events, (std::vector<Event>{
				{Reader::StartElement, "/r", ""},
				{Reader::Comment, "/r", ""},
				{Reader::EndElement, "/r", ""}}));
			check_splits(xml, allow, out);
		}
	}
}

TEST(Comments, InvalidBodiesRequireExplicitOptIn)
{
	for (std::string const body : {"a--b", "a-", "-", "--", "a---b", "--x", "a-- b"}) {
		for (bool allow : {false, true}) {
			SCOPED_TRACE(body);
			SCOPED_TRACE(allow);
			std::string xml = "<r><!--" + body + "--></r>";
			Reader reader(xml);
			if (allow) reader.set_allow_invalid_comments(true);
			auto out = drain(&reader);
			if (allow) {
				EXPECT_EQ(out.result, Result::EndDocument);
				EXPECT_EQ(out.code, Code::None);
				ASSERT_EQ(out.events.size(), 3u);
				EXPECT_EQ(std::get<0>(out.events[1]), Reader::Comment);
			} else {
				EXPECT_EQ(out.result, Result::Error);
				EXPECT_EQ(out.code, Code::InvalidSyntax);
				EXPECT_EQ(out.offset, xml.find("--", 7) + 2);
				EXPECT_FALSE(reader.error().message.empty());
				ASSERT_EQ(out.events.size(), 1u); // No invalid comment event.
			}
			check_splits(xml, allow, out);
		}
	}
}

TEST(Comments, TwoHyphensWaitForClosingBracketOrFailOnNextByte)
{
	for (char next : {'>', 'b', '-'}) {
		Reader reader;
		reader.input("<r><!--a--");
		ASSERT_TRUE(reader.next());
		EXPECT_EQ(reader.advance(), Result::NeedInput);
		EXPECT_EQ(reader.state(), Reader::None);
		EXPECT_EQ(reader.error().code, Code::None);
		reader.input(std::string(1, next));
		if (next == '>') {
			EXPECT_EQ(reader.advance(), Result::Event);
			EXPECT_EQ(reader.state(), Reader::Comment);
			reader.input("</r>");
			reader.finish_input();
			EXPECT_EQ(drain(&reader).result, Result::EndDocument);
		} else {
			EXPECT_EQ(reader.advance(), Result::Error);
			EXPECT_EQ(reader.error().code, Code::InvalidSyntax);
			EXPECT_EQ(reader.error().offset, 10u);
			EXPECT_EQ(reader.advance(), Result::Error);
			EXPECT_EQ(reader.error().offset, 10u);
		}
	}
}

TEST(Comments, UnterminatedCommentsRemainEofErrorsInBothModes)
{
	for (bool allow : {false, true}) {
		for (std::string const suffix : {"", "a", "a-", "a--"}) {
			std::string xml = "<r><!--" + suffix;
			Reader reader(xml);
			reader.set_allow_invalid_comments(allow);
			auto out = drain(&reader);
			EXPECT_EQ(out.result, Result::Error);
			EXPECT_EQ(out.code, Code::UnexpectedEof);
			EXPECT_EQ(out.offset, xml.size());
			ASSERT_EQ(out.events.size(), 1u);
			check_splits(xml, allow, out);
		}
	}
}

TEST(Comments, PermissiveModeStopsAtFirstTerminatorAndDoesNotNest)
{
	std::string xml = "<r><!--a--b<!--c--->visible<x/><!--d--></r>";
	Reader reader(xml);
	reader.set_allow_invalid_comments(true);
	auto out = drain(&reader);
	EXPECT_EQ(out.result, Result::EndDocument);
	EXPECT_EQ(out.events, (std::vector<Event>{
		{Reader::StartElement, "/r", ""}, {Reader::Comment, "/r", ""},
		{Reader::Characters, "/r", ""}, {Reader::StartElement, "/r/x", ""},
		{Reader::EndElement, "/r/x", ""}, {Reader::Comment, "/r", ""},
		{Reader::EndElement, "/r", "visible"}}));
	check_splits(xml, true, out);
}

TEST(Comments, CdataHyphensAreUnaffected)
{
	std::string xml = "<r><![CDATA[a--b--->]]></r>";
	for (bool allow : {false, true}) {
		Reader reader(xml);
		reader.set_allow_invalid_comments(allow);
		auto out = drain(&reader);
		EXPECT_EQ(out.result, Result::EndDocument);
		ASSERT_EQ(out.events.size(), 3u);
		EXPECT_EQ(std::get<2>(out.events.back()), "a--b--->");
		check_splits(xml, allow, out);
	}
}

TEST(Comments, FlagPersistsAcrossResetAndCanBeDisabledAgain)
{
	Reader reader;
	reader.set_allow_invalid_comments(true);
	reader.parse({});
	EXPECT_TRUE(reader.allow_invalid_comments());
	reader.input("<r><!--a--b--></r>");
	reader.finish_input();
	EXPECT_EQ(drain(&reader).result, Result::EndDocument);
	reader.parse({});
	reader.set_allow_invalid_comments(false);
	EXPECT_FALSE(reader.allow_invalid_comments());
	reader.input("<r><!--a--b--></r>");
	reader.finish_input();
	EXPECT_EQ(drain(&reader).code, Code::InvalidSyntax);
	reader.set_allow_invalid_comments(true);
	EXPECT_FALSE(reader.allow_invalid_comments());
	EXPECT_EQ(reader.error().code, Code::InvalidSyntax);
}

TEST(Comments, FlagCannotChangeDuringPartialTokenOrAfterEvents)
{
	for (std::string const xml : {"<", "<!--x--", "<r>", "<r/>"}) {
		Reader reader;
		reader.input(xml);
		drain(&reader);
		reader.set_allow_invalid_comments(true);
		EXPECT_EQ(reader.error().code, Code::InvalidOperation);
		EXPECT_FALSE(reader.allow_invalid_comments());
	}
	Reader reader("<r/>");
	EXPECT_EQ(drain(&reader).result, Result::EndDocument);
	reader.set_allow_invalid_comments(true);
	EXPECT_EQ(reader.error().code, Code::InvalidOperation);
}

TEST(Comments, FlagCannotChangeFromCallback)
{
	Reader reader;
	int calls = 0;
	reader.parse([&] {
		++calls;
		reader.set_allow_invalid_comments(true);
		reader.input("<r/>");
	});
	EXPECT_EQ(reader.advance(), Result::Error);
	EXPECT_EQ(reader.error().code, Code::InvalidOperation);
	EXPECT_FALSE(reader.allow_invalid_comments());
	EXPECT_EQ(reader.advance(), Result::Error);
	EXPECT_EQ(calls, 1);
}

TEST(Comments, StrictFailureDoesNotRequestMoreInput)
{
	Reader reader;
	int calls = 0;
	reader.parse([&] {
		++calls;
		reader.input("<r><!--a--b");
	});
	EXPECT_EQ(drain(&reader).code, Code::InvalidSyntax);
	EXPECT_EQ(calls, 1);
}

TEST(Comments, LimitsApplyInBothModes)
{
	for (bool allow : {false, true}) {
		Reader::Limits limits;
		limits.max_token_bytes = 10;
		Reader reader("<r><!--abcdefgh--></r>", limits);
		reader.set_allow_invalid_comments(allow);
		EXPECT_EQ(drain(&reader).code, Code::LimitExceeded);
	}
	Reader::Limits limits;
	limits.max_token_bytes = 10;
	Reader reader("<r><!--a--b--c--></r>", limits);
	reader.set_allow_invalid_comments(true);
	EXPECT_EQ(drain(&reader).code, Code::LimitExceeded);
}
