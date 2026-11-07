#include "test.h"
#include <tuple>

namespace {
using Reader = xstream::Reader;
using Limits = Reader::Limits;
using Result = Reader::ReadResult;
using Code = Reader::ErrorCode;
using Event = std::tuple<Reader::StateType, std::string, std::string, std::string>;

Limits no_retained_text()
{
	Limits limits;
	limits.max_element_text_bytes = 0;
	limits.max_total_text_bytes = 0;
	limits.max_text_parts = 0;
	return limits;
}

std::string characters(Reader const &reader)
{
	auto value = reader.characters().decode();
	return {value.begin(), value.end()};
}

void configure_input(Reader *reader, std::string const *xml, size_t *position, size_t chunk_size)
{
	if (chunk_size == 0) {
		*reader = Reader(*xml, reader->limits());
		return;
	}
	reader->parse([=] {
		if (*position == xml->size()) reader->finish_input();
		else {
			size_t size = std::min(chunk_size, xml->size() - *position);
			reader->input(std::string_view(*xml).substr(*position, size));
			*position += size;
		}
	});
}

struct Outcome {
	std::vector<Event> events;
	Result result;
	Reader::ErrorInfo error;
};

Outcome read(std::string const &xml, Limits const &limits, size_t chunk_size)
{
	Reader reader(limits);
	size_t position = 0;
	configure_input(&reader, &xml, &position, chunk_size);
	Outcome out;
	while (reader.next()) {
		if (out.events.size() >= xml.size() * 3 + 16) {
			ADD_FAILURE() << "Reader did not stop";
			break;
		}
		out.events.emplace_back(reader.state(), reader.path(), reader.text(),
			reader.is_characters() ? characters(reader) : std::string{});
		if (reader.path().empty()) {
			EXPECT_TRUE(reader.text().empty());
			EXPECT_EQ(reader.statistics().retained_text_bytes, 0u);
			EXPECT_EQ(reader.statistics().retained_text_parts, 0u);
		}
	}
	out.result = reader.result();
	out.error = reader.error();
	return out;
}
} // namespace

TEST(OutsideText, ManyCommentsWhitespaceAndInstructionsDoNotAccumulate)
{
	size_t const repetitions = 70000;
	std::string outside;
	for (size_t i = 0; i < repetitions; ++i) outside += "<!---->\n<?pi?>\t";
	std::string xml = outside + "<r/>" + outside;
	for (size_t chunk_size : {size_t(0), size_t(1)}) {
		SCOPED_TRACE(chunk_size);
		Reader reader;
		size_t position = 0, count = 0;
		configure_input(&reader, &xml, &position, chunk_size);
		while (reader.next()) {
			ASSERT_LT(count, repetitions * 8 + 2);
			if (count == repetitions * 4 || count == repetitions * 4 + 1) {
				EXPECT_EQ(reader.state(), count == repetitions * 4 ? Reader::StartElement : Reader::EndElement);
				EXPECT_EQ(reader.path(), "/r");
			} else {
				size_t index = count < repetitions * 4 ? count : count - 2;
				switch (index % 4) {
				case 0:
					EXPECT_EQ(reader.state(), Reader::Comment);
					EXPECT_TRUE(characters(reader).empty());
					break;
				case 1:
					EXPECT_EQ(reader.state(), Reader::Characters);
					EXPECT_EQ(characters(reader), "\n");
					break;
				case 2:
					EXPECT_EQ(reader.state(), Reader::Declaration);
					EXPECT_EQ(reader.name(), "?pi");
					break;
				case 3:
					EXPECT_EQ(reader.state(), Reader::Characters);
					EXPECT_EQ(characters(reader), "\t");
					break;
				}
				EXPECT_EQ(reader.path(), index % 4 == 2 ? "/?pi" : "");
			}
			EXPECT_TRUE(reader.text().empty());
			EXPECT_EQ(reader.statistics().retained_text_bytes, 0u);
			EXPECT_EQ(reader.statistics().retained_text_parts, 0u);
			++count;
		}
		EXPECT_EQ(count, repetitions * 8 + 2);
		EXPECT_EQ(reader.result(), Result::EndDocument) << reader.error().message;
		EXPECT_EQ(reader.error().code, Code::None);
		EXPECT_EQ(reader.statistics().retained_text_bytes, 0u);
		EXPECT_EQ(reader.statistics().retained_text_parts, 0u);
	}
}

TEST(OutsideText, ZeroRetentionBudgetsStillAllowOutsideWhitespace)
{
	std::string xml = " \t<!--x-->\r<?pi?>\n<r/>\n<!---->\t";
	std::vector<Event> expected = {
		{Reader::Characters, "", "", " \t"},
		{Reader::Comment, "", "", ""},
		{Reader::Characters, "", "", "\r"},
		{Reader::Declaration, "/?pi", "", ""},
		{Reader::Characters, "", "", "\n"},
		{Reader::StartElement, "/r", "", ""},
		{Reader::EndElement, "/r", "", ""},
		{Reader::Characters, "", "", "\n"},
		{Reader::Comment, "", "", ""},
		{Reader::Characters, "", "", "\t"}
	};
	std::vector<Limits> configurations = {Limits{}, no_retained_text()};
	for (size_t Limits::*field : {&Limits::max_element_text_bytes, &Limits::max_total_text_bytes, &Limits::max_text_parts}) {
		Limits limits;
		limits.*field = 0;
		configurations.push_back(limits);
	}
	for (auto const &limits : configurations) {
		for (size_t chunk_size = 0; chunk_size <= xml.size(); ++chunk_size) {
			SCOPED_TRACE(chunk_size);
			auto outcome = read(xml, limits, chunk_size);
			EXPECT_EQ(outcome.events, expected);
			EXPECT_EQ(outcome.result, Result::EndDocument);
			EXPECT_EQ(outcome.error.code, Code::None);
		}
	}
}

TEST(OutsideText, ElementTextRetainsItsOwnBudgetsAndValues)
{
	Limits limits;
	limits.max_element_text_bytes = 1;
	limits.max_total_text_bytes = 2;
	limits.max_text_parts = 2;
	std::string xml = " \n<r>a<c>b</c></r>\t";
	std::vector<Event> expected = {
		{Reader::Characters, "", "", " \n"},
		{Reader::StartElement, "/r", "", ""},
		{Reader::Characters, "/r", "a", "a"},
		{Reader::StartElement, "/r/c", "", ""},
		{Reader::Characters, "/r/c", "b", "b"},
		{Reader::EndElement, "/r/c", "b", ""},
		{Reader::EndElement, "/r", "a", ""},
		{Reader::Characters, "", "", "\t"}
	};
	for (size_t chunk_size : {size_t(0), size_t(1)}) {
		auto outcome = read(xml, limits, chunk_size);
		EXPECT_EQ(outcome.events, expected);
		EXPECT_EQ(outcome.result, Result::EndDocument);
		EXPECT_EQ(outcome.error.code, Code::None);
		auto invalid = read(" \n<r>ab</r>\t", limits, chunk_size);
		EXPECT_EQ(invalid.events.size(), 2u);
		EXPECT_EQ(invalid.result, Result::Error);
		EXPECT_EQ(invalid.error.code, Code::LimitExceeded);
		EXPECT_EQ(invalid.error.offset, 6u);
	}
}

TEST(OutsideText, TokenDocumentAndBufferLimitsStillApply)
{
	Limits limits = no_retained_text();
	limits.max_token_bytes = 4;
	for (size_t chunk_size : {size_t(0), size_t(1)}) {
		auto valid = read("    <r/>    ", limits, chunk_size);
		EXPECT_EQ(valid.events.size(), 4u);
		EXPECT_EQ(valid.result, Result::EndDocument);
		for (std::string const xml : {"     <r/>", "<r/>     "}) {
			auto invalid = read(xml, limits, chunk_size);
			EXPECT_EQ(invalid.events.size(), xml.front() == '<' ? 2u : 0u);
			EXPECT_EQ(invalid.result, Result::Error);
			EXPECT_EQ(invalid.error.code, Code::LimitExceeded);
			EXPECT_EQ(invalid.error.offset, xml.front() == '<' ? 8u : 4u);
		}
	}
	limits = no_retained_text();
	limits.max_document_bytes = 8;
	for (size_t chunk_size : {size_t(0), size_t(1)}) {
		EXPECT_EQ(read("    <r/>", limits, chunk_size).result, Result::EndDocument);
		auto invalid = read("     <r/>", limits, chunk_size);
		EXPECT_EQ(invalid.result, Result::Error);
		EXPECT_EQ(invalid.error.code, Code::LimitExceeded);
		EXPECT_EQ(invalid.error.offset, 8u);
	}
	limits = no_retained_text();
	limits.max_buffer_bytes = 4;
	EXPECT_EQ(read("   <r/>", limits, 1).result, Result::EndDocument);
	EXPECT_EQ(read("     <r/>", limits, 0).result, Result::EndDocument);
	auto invalid = read("     <r/>", limits, 1);
	EXPECT_TRUE(invalid.events.empty());
	EXPECT_EQ(invalid.result, Result::Error);
	EXPECT_EQ(invalid.error.code, Code::LimitExceeded);
	EXPECT_EQ(invalid.error.offset, 4u);
}

TEST(OutsideText, MissingRootAndInvalidCharactersStillFailWithoutAccessors)
{
	struct Case { std::string xml; Code code; size_t offset; size_t events; };
	for (auto const &test : std::vector<Case>{
		{" \t\n", Code::UnexpectedEof, 3, 1},
		{"\t<", Code::UnexpectedEof, 2, 1},
		{" x<r/>", Code::InvalidSyntax, 1, 0},
		{"<r/> x", Code::InvalidSyntax, 5, 2},
		{"\x80<r/>", Code::InvalidUtf8, 0, 0},
		{"<r/>\x01", Code::InvalidCharacter, 4, 2}}) {
		for (size_t chunk_size : {size_t(0), size_t(1)}) {
			Reader reader(no_retained_text());
			size_t position = 0, count = 0;
			configure_input(&reader, &test.xml, &position, chunk_size);
			while (reader.next()) ASSERT_LT(count++, test.xml.size() * 3 + 16);
			EXPECT_EQ(count, test.events);
			EXPECT_EQ(reader.result(), Result::Error);
			EXPECT_EQ(reader.error().code, test.code);
			EXPECT_EQ(reader.error().offset, test.offset);
			auto first = reader.error();
			reader.input_error("later I/O error");
			reader.input("ignored");
			EXPECT_EQ(reader.advance(), Result::Error);
			EXPECT_EQ(reader.error().code, first.code);
			EXPECT_EQ(reader.error().offset, first.offset);
			EXPECT_EQ(reader.error().message, first.message);
		}
	}
}

TEST(OutsideText, OwnedCharactersSurviveInputCopyHoldAndReset)
{
	Reader reader(no_retained_text());
	reader.input(" \t<");
	ASSERT_TRUE(reader.next());
	ASSERT_TRUE(reader.is_characters());
	auto leading = reader.characters();
	Reader copy = reader;
	reader.hold();
	ASSERT_TRUE(reader.next());
	EXPECT_EQ(characters(reader), " \t");
	EXPECT_EQ(characters(copy), " \t");
	reader.input("r/><!--" + std::string(600, 'x') + "-->\n");
	EXPECT_EQ(characters(reader), " \t");
	ASSERT_TRUE(reader.next());
	ASSERT_TRUE(reader.is_start_element("r"));
	EXPECT_TRUE(characters(reader).empty());
	ASSERT_TRUE(reader.next());
	ASSERT_TRUE(reader.is_end_element("r"));
	ASSERT_TRUE(reader.next());
	ASSERT_EQ(reader.state(), Reader::Comment);
	EXPECT_TRUE(characters(reader).empty());
	EXPECT_EQ(reader.advance(), Result::NeedInput);
	reader.finish_input();
	ASSERT_TRUE(reader.next());
	ASSERT_TRUE(reader.is_characters());
	auto trailing = reader.characters();
	reader.parse([] {});
	EXPECT_TRUE(characters(reader).empty());
	EXPECT_TRUE(reader.text().empty());
	EXPECT_EQ(reader.statistics().retained_text_bytes, 0u);
	EXPECT_EQ(reader.statistics().retained_text_parts, 0u);
	copy.input("r/>");
	copy.finish_input();
	ASSERT_TRUE(copy.next());
	ASSERT_TRUE(copy.is_start_element("r"));
	EXPECT_TRUE(characters(copy).empty());
	ASSERT_TRUE(copy.next());
	ASSERT_TRUE(copy.is_end_element("r"));
	EXPECT_FALSE(copy.next());
	EXPECT_EQ(copy.result(), Result::EndDocument);
	EXPECT_TRUE(characters(copy).empty());
	EXPECT_EQ(characters(reader), "");
	auto leading_bytes = leading.decode();
	auto trailing_bytes = trailing.decode();
	EXPECT_EQ(std::string(leading_bytes.begin(), leading_bytes.end()), " \t");
	EXPECT_EQ(std::string(trailing_bytes.begin(), trailing_bytes.end()), "\n");
}

TEST(OutsideText, UnfinishedWhitespaceWaitsWithoutPublishingOrRescanning)
{
	Reader reader(no_retained_text());
	reader.input(std::string(300, ' '));
	EXPECT_EQ(reader.advance(), Result::NeedInput);
	EXPECT_TRUE(characters(reader).empty());
	auto stats = reader.statistics();
	for (int i = 0; i < 5; ++i) {
		EXPECT_EQ(reader.advance(), Result::NeedInput);
		EXPECT_EQ(reader.statistics().scan_steps, stats.scan_steps);
	}
	reader.input("<r");
	ASSERT_TRUE(reader.next());
	ASSERT_TRUE(reader.is_characters());
	EXPECT_EQ(characters(reader), std::string(300, ' '));
	EXPECT_TRUE(reader.text().empty());
	EXPECT_EQ(reader.advance(), Result::NeedInput);
	EXPECT_TRUE(characters(reader).empty());
	reader.input("/>");
	ASSERT_TRUE(reader.next());
	ASSERT_TRUE(reader.is_start_element("r"));
	ASSERT_TRUE(reader.next());
	ASSERT_TRUE(reader.is_end_element("r"));
	EXPECT_EQ(reader.advance(), Result::NeedInput);
	reader.finish_input();
	EXPECT_EQ(reader.advance(), Result::EndDocument);
	EXPECT_EQ(reader.error().code, Code::None);
	EXPECT_EQ(reader.statistics().retained_text_bytes, 0u);
	EXPECT_EQ(reader.statistics().retained_text_parts, 0u);
}
