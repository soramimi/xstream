#include "test.h"
#include <gtest/gtest.h>
#include <tuple>
#include <stdexcept>

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
	Outcome outcome;
	while (reader->advance() == Result::Event) {
		if (outcome.events.size() > 1000) {
			ADD_FAILURE() << "Reader did not stop";
			break;
		}
		std::vector<std::pair<std::string, std::string>> attributes;
		for (auto const &attribute : reader->attributes()) {
			attributes.emplace_back(attribute.first, attribute.second.to_string());
		}
		std::string text;
		if (reader->is_characters()) {
			auto value = reader->characters().decode();
			text.assign(value.begin(), value.end());
		} else if (reader->is_end_element()) {
			text = reader->text();
		}
		outcome.events.emplace_back(reader->state(), reader->name(), reader->path(), attributes, text);
	}
	outcome.result = reader->result();
	outcome.code = reader->error().code;
	outcome.offset = reader->error().offset;
	return outcome;
}

Outcome stream(std::vector<std::string> const &chunks)
{
	Reader reader;
	size_t pos = 0;
	reader.parse([&] {
		if (pos < chunks.size()) reader.input(chunks[pos++]);
		else reader.finish_input();
	});
	return drain(&reader);
}

void expect_same(Outcome const &actual, Outcome const &expected)
{
	EXPECT_EQ(actual.events, expected.events);
	EXPECT_EQ(actual.result, expected.result);
	EXPECT_EQ(actual.code, expected.code);
	EXPECT_EQ(actual.offset, expected.offset);
}

} // namespace

TEST(ReadResults, OneShotInputEndsWithoutRequestingMoreData)
{
	Reader reader("<root/> \n");
	auto outcome = drain(&reader);
	ASSERT_EQ(outcome.events.size(), 3u);
	EXPECT_EQ(outcome.result, Result::EndDocument);
	EXPECT_EQ(reader.state(), Reader::None);
	EXPECT_EQ(reader.error().code, Code::None);
	EXPECT_FALSE(reader.is_not_enough_input());
	reader.hold();
	for (int i = 0; i < 3; ++i) {
		EXPECT_FALSE(reader.next());
		EXPECT_EQ(reader.result(), Result::EndDocument);
	}
}

TEST(ReadResults, PushInputWaitsForExplicitEof)
{
	Reader reader;
	EXPECT_EQ(reader.advance(), Result::NeedInput);
	reader.input("<root/>");
	EXPECT_TRUE(reader.next());
	EXPECT_TRUE(reader.next());
	EXPECT_FALSE(reader.next());
	EXPECT_EQ(reader.result(), Result::NeedInput);
	EXPECT_TRUE(reader.is_not_enough_input());
	reader.input({});
	EXPECT_EQ(reader.advance(), Result::NeedInput);
	reader.finish_input();
	reader.finish_input();
	EXPECT_EQ(reader.advance(), Result::EndDocument);
	EXPECT_FALSE(reader.is_not_enough_input());
}

TEST(ReadResults, EofDetectsUnclosedElementsAndIncompleteTokens)
{
	for (std::string const xml : {"", " \n", "<?xml version='1.0'?>", "<root>",
		"<root", "<root attr='value", "<root>text", "<root></roo",
		"<root><![CDATA[text", "<root><!--comment", "<root><?pi attr='x'", "<root/"}) {
		SCOPED_TRACE(xml);
		Reader reader(xml);
		auto outcome = drain(&reader);
		EXPECT_EQ(outcome.result, Result::Error);
		EXPECT_EQ(outcome.code, Code::UnexpectedEof);
		EXPECT_EQ(outcome.offset, xml.size());
		EXPECT_FALSE(reader.is_not_enough_input());
		EXPECT_FALSE(reader.error().message.empty());
	}
}

TEST(ReadResults, IncompleteTokenCanWaitAndResumeOrFailAtEof)
{
	for (bool complete : {false, true}) {
		std::string prefix = "<root attr='value";
		Reader reader;
		reader.input(prefix);
		EXPECT_EQ(reader.advance(), Result::NeedInput);
		EXPECT_EQ(reader.state(), Reader::None);
		EXPECT_EQ(reader.error().code, Code::None);
		if (complete) reader.input("'/>");
		reader.finish_input();
		auto outcome = drain(&reader);
		EXPECT_EQ(outcome.result, complete ? Result::EndDocument : Result::Error);
		EXPECT_EQ(outcome.code, complete ? Code::None : Code::UnexpectedEof);
		if (!complete) {
			EXPECT_EQ(outcome.offset, prefix.size());
		}
	}
}

TEST(ReadResults, InvalidSyntaxDoesNotWaitForEofOrCallForMoreInput)
{
	for (std::string const xml : {"<root><@", "<>", "< root>", "<root attr=>",
		"<root attr=value>", "<root attr>", "<root attr='x'bad='y'>",
		"<root /x", "<?xml>", "<root></root extra>", "text<root/>",
		"<root/>tail", "<root/><second/>", "<![CDATA[text]]>"}) {
		SCOPED_TRACE(xml);
		size_t calls = 0;
		Reader reader;
		reader.parse([&] { ++calls; });
		reader.input(xml);
		auto outcome = drain(&reader);
		EXPECT_EQ(outcome.result, Result::Error);
		EXPECT_EQ(outcome.code, Code::InvalidSyntax);
		EXPECT_LT(outcome.offset, xml.size());
		EXPECT_EQ(calls, 0u);
		EXPECT_FALSE(reader.is_not_enough_input());
	}
}

TEST(ReadResults, EofAtEveryByteMatchesOneShotInput)
{
	std::string xml = "<?xml version='1.0'?><root attr='&amp;'>text<![CDATA[<raw>]]>"
		"<!--comment--><child/></root> \n";
	for (size_t length = 0; length <= xml.size(); ++length) {
		SCOPED_TRACE(length);
		std::string prefix = xml.substr(0, length);
		Reader reader(prefix);
		auto expected = drain(&reader);
		std::vector<std::string> bytes;
		for (char c : prefix) bytes.emplace_back(1, c);
		expect_same(stream(bytes), expected);
	}
}

TEST(ReadResults, EverySplitPreservesEventsAndFinalResult)
{
	for (std::string const xml : {
		"<root/> \n<!--after-->",
		"<?xml version='1.0'?><root attr = 'value'>text<child/></root>",
		"<root><child></root>", "<root>text<!--unfinished", "<root attr='x'bad='y'>"}) {
		SCOPED_TRACE(xml);
		Reader reader(xml);
		auto expected = drain(&reader);
		for (size_t split = 1; split < xml.size(); ++split) {
			SCOPED_TRACE(split);
			expect_same(stream({xml.substr(0, split), xml.substr(split)}), expected);
		}
	}
}

TEST(ReadResults, PositionsSurviveCompactionAndReallocation)
{
	std::string xml = "<root>" + std::string(500, 'x') + "</wrong>";
	Reader reader(xml);
	auto expected = drain(&reader);
	ASSERT_EQ(expected.code, Code::MismatchedEndTag);
	ASSERT_EQ(expected.offset, xml.find("</wrong>"));
	std::vector<std::string> bytes;
	for (char c : xml) bytes.emplace_back(1, c);
	expect_same(stream(bytes), expected);
	expect_same(stream({xml.substr(0, 6), xml.substr(6, 300), xml.substr(306)}), expected);

	std::string invalid = "<root>text<child attr='x'bad='y'>";
	Reader bad(invalid);
	auto syntax = drain(&bad);
	ASSERT_EQ(syntax.code, Code::InvalidSyntax);
	ASSERT_EQ(syntax.offset, invalid.find("bad"));
	expect_same(stream({invalid.substr(0, 6), invalid.substr(6)}), syntax);
}

TEST(ReadResults, CallbackCanFinishWithOrWithoutALastChunk)
{
	for (bool finish_with_data : {false, true}) {
		size_t calls = 0;
		Reader reader;
		reader.parse([&] {
			if (++calls == 1) reader.input("<root/>");
			if (finish_with_data || calls == 2) reader.finish_input();
		});
		EXPECT_EQ(drain(&reader).result, Result::EndDocument);
		EXPECT_EQ(calls, finish_with_data ? 1u : 2u);
		EXPECT_EQ(reader.advance(), Result::EndDocument);
		EXPECT_EQ(calls, finish_with_data ? 1u : 2u);
	}
}

TEST(ReadResults, NoProgressCanResumeWithMoreDataOrEof)
{
	Reader reader;
	size_t calls = 0;
	reader.parse([&] { ++calls; reader.input({}); });
	EXPECT_EQ(reader.advance(), Result::NeedInput);
	EXPECT_EQ(calls, 1u);
	reader.input("<root");
	EXPECT_EQ(reader.advance(), Result::NeedInput);
	EXPECT_EQ(calls, 2u);
	reader.input("/>");
	EXPECT_EQ(drain(&reader).result, Result::NeedInput);
	EXPECT_EQ(calls, 3u);
	reader.finish_input();
	EXPECT_EQ(reader.advance(), Result::EndDocument);
	EXPECT_EQ(calls, 3u);
}

TEST(ReadResults, EmptyInputClearsWaitingAndYieldsOnceWithoutACallback)
{
	Reader reader;
	EXPECT_FALSE(reader.next());
	EXPECT_TRUE(reader.is_not_enough_input());
	reader.input({});
	EXPECT_FALSE(reader.is_not_enough_input());
	EXPECT_EQ(reader.result(), Result::NeedInput);
	EXPECT_TRUE(reader.next());
	EXPECT_EQ(reader.state(), Reader::None);
	EXPECT_EQ(reader.result(), Result::NeedInput);
	EXPECT_FALSE(reader.is_not_enough_input());
	EXPECT_EQ(reader.statistics().buffer_capacity, 0u);
	EXPECT_EQ(reader.statistics().copied_input_bytes, 0u);
	EXPECT_FALSE(reader.next());
	EXPECT_TRUE(reader.is_not_enough_input());
	reader.finish_input();
	EXPECT_FALSE(reader.next());
	EXPECT_EQ(reader.result(), Result::Error);
	EXPECT_EQ(reader.error().code, Code::UnexpectedEof);
	EXPECT_EQ(reader.error().offset, 0u);
}

TEST(ReadResults, EmptyCallbackInputReturnsToCallerWithoutRetrying)
{
	Reader reader;
	size_t calls = 0;
	reader.parse([&] {
		if (++calls <= 2) reader.input({});
	});
	for (size_t expected = 1; expected <= 2; ++expected) {
		EXPECT_TRUE(reader.next());
		EXPECT_EQ(calls, expected);
		EXPECT_EQ(reader.state(), Reader::None);
		EXPECT_EQ(reader.result(), Result::NeedInput);
		EXPECT_FALSE(reader.is_not_enough_input());
	}
	EXPECT_FALSE(reader.next());
	EXPECT_EQ(calls, 3u);
	EXPECT_EQ(reader.state(), Reader::None);
	EXPECT_EQ(reader.result(), Result::NeedInput);
	EXPECT_TRUE(reader.is_not_enough_input());
	EXPECT_EQ(reader.statistics().scan_steps, 0u);
}

TEST(ReadResults, EmptyChunksPreservePartialTokensAndCompleteEvents)
{
	Reader reader;
	std::vector<std::string> chunks = {"", "<r", "", "", " a='&amp;'>", "", "x", "</r>", ""};
	size_t chunk = 0, calls = 0, steps = 0, yields = 0;
	reader.parse([&] {
		++calls;
		if (chunk < chunks.size()) reader.input(chunks[chunk++]);
		else reader.finish_input();
	});
	std::vector<Reader::StateType> events;
	while (reader.next()) {
		ASSERT_LT(++steps, 32u);
		if (reader.result() == Result::NeedInput) {
			++yields;
			EXPECT_EQ(reader.state(), Reader::None);
			EXPECT_FALSE(reader.is_not_enough_input());
			continue;
		}
		EXPECT_EQ(reader.result(), Result::Event);
		events.push_back(reader.state());
		EXPECT_EQ(reader.name(), "r");
		EXPECT_EQ(reader.path(), "/r");
		EXPECT_EQ(reader.attribute("a", ""), "&");
		if (reader.is_characters()) {
			auto bytes = reader.characters().decode();
			EXPECT_EQ(std::string(bytes.begin(), bytes.end()), "x");
		}
		if (reader.is_end_element()) { EXPECT_EQ(reader.text(), "x"); }
	}
	EXPECT_EQ(events, (std::vector<Reader::StateType>{Reader::StartElement, Reader::Characters, Reader::EndElement}));
	EXPECT_EQ(yields, 5u);
	EXPECT_EQ(calls, chunks.size() + 1);
	EXPECT_EQ(chunk, chunks.size());
	EXPECT_EQ(reader.result(), Result::EndDocument);
	EXPECT_EQ(reader.error().code, Code::None);
}

TEST(ReadResults, LastCallbackDeterminesWhetherEmptyInputWasSupplied)
{
	Reader reader;
	size_t calls = 0;
	reader.input({});
	reader.parse([&] {
		if (++calls == 1) {
			reader.input("<r");
			reader.input({});
		}
	});
	EXPECT_FALSE(reader.next());
	EXPECT_EQ(calls, 2u);
	EXPECT_EQ(reader.state(), Reader::None);
	EXPECT_TRUE(reader.is_not_enough_input());
	reader.input("/>");
	ASSERT_TRUE(reader.next());
	ASSERT_TRUE(reader.is_start_element("r"));
	ASSERT_TRUE(reader.next());
	ASSERT_TRUE(reader.is_end_element("r"));
	reader.finish_input();
	EXPECT_FALSE(reader.next());
	EXPECT_EQ(reader.result(), Result::EndDocument);
	reader.parse([&] { ++calls; });
	EXPECT_FALSE(reader.next());
	EXPECT_TRUE(reader.is_not_enough_input());
}

TEST(ReadResults, EmptyInputDoesNotHideEofOrSourceErrors)
{
	for (bool source_error : {false, true}) {
		Reader reader;
		size_t calls = 0;
		reader.parse([&] {
			++calls;
			reader.input({});
			if (source_error) reader.input_error("source error");
			else reader.finish_input();
		});
		reader.input("<r/>");
		ASSERT_TRUE(reader.next());
		ASSERT_TRUE(reader.is_start_element("r"));
		ASSERT_TRUE(reader.next());
		ASSERT_TRUE(reader.is_end_element("r"));
		EXPECT_FALSE(reader.next());
		EXPECT_EQ(reader.result(), source_error ? Result::Error : Result::EndDocument);
		EXPECT_EQ(reader.error().code, source_error ? Code::InputError : Code::None);
		EXPECT_FALSE(reader.is_not_enough_input());
		auto first = reader.error();
		reader.input({});
		EXPECT_FALSE(reader.next());
		EXPECT_EQ(calls, 1u);
		EXPECT_EQ(reader.error().code, first.code);
		EXPECT_EQ(reader.error().offset, first.offset);
		EXPECT_EQ(reader.error().message, first.message);
	}
}

TEST(ReadResults, ParseClearsPendingEmptyInputNotifications)
{
	Reader reader;
	EXPECT_FALSE(reader.next());
	reader.input({});
	reader.parse([] {});
	EXPECT_FALSE(reader.next());
	EXPECT_EQ(reader.state(), Reader::None);
	EXPECT_EQ(reader.result(), Result::NeedInput);
	EXPECT_TRUE(reader.is_not_enough_input());
}

TEST(ReadResults, EmptyInputDoesNotOverrideSubtreeEnd)
{
	Reader reader;
	reader.input("<r><c/></r>");
	ASSERT_TRUE(reader.next());
	ASSERT_TRUE(reader.is_start_element("r"));
	reader.nest();
	ASSERT_TRUE(reader.next());
	ASSERT_TRUE(reader.is_start_element("c"));
	ASSERT_TRUE(reader.next());
	ASSERT_TRUE(reader.is_end_element("c"));
	reader.input({});
	EXPECT_FALSE(reader.next());
	EXPECT_EQ(reader.result(), Result::SubtreeEnd);
	EXPECT_FALSE(reader.is_not_enough_input());
	ASSERT_TRUE(reader.next());
	ASSERT_TRUE(reader.is_end_element("r"));
	EXPECT_EQ(reader.result(), Result::Event);
	reader.finish_input();
	EXPECT_FALSE(reader.next());
	EXPECT_EQ(reader.result(), Result::EndDocument);
}

TEST(ReadResults, RootClosureAloneDoesNotFinishTheDocument)
{
	Reader reader;
	reader.input("<root/>");
	EXPECT_EQ(drain(&reader).result, Result::NeedInput);
	reader.input("<");
	EXPECT_EQ(reader.advance(), Result::NeedInput);
	reader.finish_input();
	EXPECT_EQ(reader.advance(), Result::Error);
	EXPECT_EQ(reader.error().code, Code::UnexpectedEof);
	EXPECT_EQ(reader.error().offset, 8u);
}

TEST(ReadResults, InputAfterEofIsRejectedWithoutReplacingAnExistingError)
{
	for (bool drained : {false, true}) {
		Reader reader;
		reader.input("<root/>");
		reader.finish_input();
		if (drained) {
			ASSERT_EQ(drain(&reader).result, Result::EndDocument);
		}
		reader.input("<other/>");
		EXPECT_EQ(reader.result(), Result::Error);
		EXPECT_EQ(reader.error().code, Code::InvalidOperation);
		auto first_error = reader.error();
		reader.input_error("a later error");
		reader.input("ignored");
		reader.finish_input();
		reader.hold();
		EXPECT_EQ(reader.advance(), Result::Error);
		EXPECT_EQ(reader.error().code, first_error.code);
		EXPECT_EQ(reader.error().offset, first_error.offset);
		EXPECT_EQ(reader.error().message, first_error.message);
	}
}

TEST(ReadResults, IoErrorsStopCallbacksAndRetainTheirMessage)
{
	Reader reader;
	size_t calls = 0;
	reader.parse([&] {
		if (++calls == 1) reader.input("<root>");
		else reader.input_error("read failed");
	});
	EXPECT_TRUE(reader.next());
	EXPECT_FALSE(reader.next());
	EXPECT_EQ(reader.result(), Result::Error);
	EXPECT_EQ(reader.error().code, Code::InputError);
	EXPECT_EQ(reader.error().offset, 6u);
	EXPECT_EQ(reader.error().message, "read failed");
	EXPECT_EQ(reader.advance(), Result::Error);
	EXPECT_EQ(calls, 2u);
}

TEST(ReadResults, SubtreeEndIsDistinctFromInputWaitingAndDocumentEnd)
{
	Reader reader("<root><branch/></root>");
	ASSERT_TRUE(reader.next());
	ASSERT_TRUE(reader.next());
	reader.nest();
	EXPECT_FALSE(reader.next());
	EXPECT_EQ(reader.result(), Result::SubtreeEnd);
	EXPECT_FALSE(reader.is_not_enough_input());
	EXPECT_TRUE(reader.match_end("/root/branch"));
	EXPECT_EQ(reader.advance(), Result::Event);
	EXPECT_TRUE(reader.match_end("/root/branch"));
	EXPECT_TRUE(reader.next());
	EXPECT_TRUE(reader.match_end("/root"));
	EXPECT_EQ(reader.advance(), Result::EndDocument);

	Reader truncated("<root><branch>");
	ASSERT_TRUE(truncated.next());
	ASSERT_TRUE(truncated.next());
	truncated.nest();
	EXPECT_EQ(truncated.advance(), Result::Error);
	EXPECT_EQ(truncated.error().code, Code::UnexpectedEof);
}

TEST(ReadResults, ParseClearsFinalResultErrorAndInputPosition)
{
	for (bool failed : {false, true}) {
		Reader reader(failed ? "<root>" : "<root/>");
		(void)drain(&reader);
		reader.parse([] {});
		EXPECT_EQ(reader.error().code, Code::None);
		EXPECT_EQ(reader.error().offset, 0u);
		EXPECT_EQ(reader.state(), Reader::None);
		EXPECT_EQ(reader.advance(), Result::NeedInput);
		reader.input("<fresh/>");
		reader.finish_input();
		EXPECT_EQ(drain(&reader).result, Result::EndDocument);
		EXPECT_EQ(reader.depth(), 1);
	}
}

TEST(ReadResults, CallbackReentryIsRejectedWithoutDestroyingTheCallback)
{
	for (int operation = 0; operation < 5; ++operation) {
		SCOPED_TRACE(operation);
		Reader reader;
		reader.parse([&] {
			switch (operation) {
			case 0: reader.next(); break;
			case 1: reader.advance(); break;
			case 2: reader.parse([] {}); break;
			case 3: reader.nest(); break;
			case 4: reader.hold(); break;
			}
		});
		EXPECT_EQ(reader.advance(), Result::Error);
		EXPECT_EQ(reader.error().code, Code::InvalidOperation);
		EXPECT_EQ(reader.advance(), Result::Error);
	}
}

TEST(ReadResults, CallbackExceptionsReleaseTheAdvancingGuard)
{
	Reader reader;
	bool throw_now = true;
	reader.parse([&] {
		if (throw_now) throw std::runtime_error("callback failed");
		reader.input("<root/>");
		reader.finish_input();
	});
	EXPECT_THROW(reader.advance(), std::runtime_error);
	throw_now = false;
	EXPECT_EQ(drain(&reader).result, Result::EndDocument);
	EXPECT_EQ(reader.error().code, Code::None);
}
