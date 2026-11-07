#include "test.h"
#include <gtest/gtest.h>
#include <tuple>

namespace {
using Reader = xstream::Reader;
using Limits = Reader::Limits;
using Result = Reader::ReadResult;
using Code = Reader::ErrorCode;

void drain(Reader *reader)
{
	size_t events = 0;
	while (reader->next()) ASSERT_LT(++events, 100000u);
}

struct Outcome {
	Result result;
	Reader::ErrorInfo error;
	Reader::Statistics statistics;
};

Outcome run(std::string const &xml, Limits const &limits, size_t chunk_size)
{
	if (chunk_size == 0) {
		Reader reader(xml, limits);
		drain(&reader);
		return {reader.result(), reader.error(), reader.statistics()};
	}
	Reader reader(limits);
	size_t pos = 0;
	reader.parse([&] {
		if (pos < xml.size()) {
			size_t size = std::min(chunk_size, xml.size() - pos);
			reader.input(std::string_view(xml.data() + pos, size));
			pos += size;
		} else reader.finish_input();
	});
	drain(&reader);
	return {reader.result(), reader.error(), reader.statistics()};
}

using Event = std::tuple<Reader::StateType, std::string, std::string,
	std::vector<std::pair<std::string, std::string>>, std::string>;
std::vector<Event> events(Reader *reader)
{
	std::vector<Event> out;
	while (reader->next()) {
		if (out.size() >= 1000) { ADD_FAILURE() << "Too many events"; break; }
		std::vector<std::pair<std::string, std::string>> attributes;
		for (auto const &a : reader->attributes()) attributes.emplace_back(a.first, a.second.to_string());
		std::string text;
		if (reader->is_characters()) {
			auto value = reader->characters().decode();
			text.assign(value.begin(), value.end());
		} else if (reader->is_end_element()) text = reader->text();
		out.emplace_back(reader->state(), reader->name(), reader->path(), attributes, text);
	}
	return out;
}
} // namespace

TEST(ResourceLimits, BeforeAtAndAfterEveryContentBoundary)
{
	struct Case { size_t Limits::*field; size_t needed; std::string xml; };
	std::vector<Case> cases = {
		{&Limits::max_depth, 2, "<r><a/></r>"},
		{&Limits::max_name_bytes, 4, "<long/>"},
		{&Limits::max_name_bytes, 4, "<r long='x'/>"},
		{&Limits::max_attributes, 2, "<r a='1' b='2'/>"},
		{&Limits::max_attribute_value_bytes, 4, "<r a='abcd'/>"},
		{&Limits::max_token_bytes, 7, "<root/>"},
		{&Limits::max_element_text_bytes, 4, "<r>ab<![CDATA[cd]]></r>"},
		{&Limits::max_total_text_bytes, 4, "<r>ab<c>cd</c></r>"},
		{&Limits::max_text_parts, 2, "<r>a<![CDATA[b]]></r>"},
		{&Limits::max_total_attribute_bytes, 4, "<r a='b'><c d='e'/></r>"},
		{&Limits::max_path_bytes, 4, "<r><c/></r>"},
		{&Limits::max_document_bytes, 7, "<root/>"}
	};
	for (auto const &item : cases) {
		SCOPED_TRACE(item.xml);
		for (size_t maximum = item.needed - 1; maximum <= item.needed + 1; ++maximum) {
			SCOPED_TRACE(maximum);
			Limits limits;
			limits.*item.field = maximum;
			for (size_t chunk_size : {size_t(0), size_t(1)}) {
				SCOPED_TRACE(chunk_size);
				auto outcome = run(item.xml, limits, chunk_size);
				bool fits = maximum >= item.needed;
				EXPECT_EQ(outcome.result, fits ? Result::EndDocument : Result::Error);
				EXPECT_EQ(outcome.error.code, fits ? Code::None : Code::LimitExceeded);
				EXPECT_LE(outcome.statistics.buffer_capacity, limits.max_buffer_bytes);
			}
		}
	}
}

TEST(ResourceLimits, BufferAndDocumentLimitsAreCheckedBeforeCopying)
{
	for (size_t maximum : {size_t(6), size_t(7), size_t(8)}) {
		Limits limits;
		limits.max_buffer_bytes = maximum;
		for (size_t chunk_size : {size_t(1), size_t(7)}) {
			auto outcome = run("<root/>", limits, chunk_size);
			EXPECT_EQ(outcome.result, maximum >= 7 ? Result::EndDocument : Result::Error);
			EXPECT_LE(outcome.statistics.buffer_capacity, maximum);
			if (maximum == 6) {
				EXPECT_EQ(outcome.error.code, Code::LimitExceeded);
				EXPECT_EQ(outcome.error.offset, 6u);
				EXPECT_LE(outcome.statistics.copied_input_bytes, 6u);
				if (chunk_size == 7) {
					EXPECT_EQ(outcome.statistics.buffer_capacity, 0u);
				}
			}
		}
	}
	Limits limits;
	limits.max_document_bytes = 6;
	auto outcome = run("<root/>", limits, 7);
	EXPECT_EQ(outcome.error.code, Code::LimitExceeded);
	EXPECT_EQ(outcome.statistics.buffer_capacity, 0u);
	EXPECT_EQ(outcome.statistics.copied_input_bytes, 0u);
	EXPECT_EQ(outcome.statistics.scan_steps, 0u);
	Reader borrowed("<root/>", limits);
	EXPECT_EQ(borrowed.error().code, Code::LimitExceeded);
	EXPECT_EQ(borrowed.statistics().buffer_capacity, 0u);
}

TEST(ResourceLimits, BorrowedInputDoesNotRequireAStreamingBuffer)
{
	Limits limits;
	limits.max_buffer_bytes = 0;
	Reader reader("<root/>", limits);
	drain(&reader);
	EXPECT_EQ(reader.result(), Result::EndDocument);
	EXPECT_EQ(reader.statistics().buffer_capacity, 0u);
	EXPECT_EQ(reader.statistics().copied_input_bytes, 0u);
}

TEST(ResourceLimits, ZeroMeansNoAllowanceRatherThanUnlimited)
{
	for (size_t Limits::*field : {&Limits::max_depth, &Limits::max_name_bytes,
		&Limits::max_token_bytes, &Limits::max_path_bytes, &Limits::max_document_bytes}) {
		Limits limits;
		limits.*field = 0;
		EXPECT_EQ(run("<r/>", limits, 1).error.code, Code::LimitExceeded);
	}
	Limits limits;
	limits.max_attributes = 0;
	limits.max_element_text_bytes = 0;
	limits.max_total_text_bytes = 0;
	EXPECT_EQ(run("<r/>", limits, 1).result, Result::EndDocument);
	EXPECT_EQ(run("<r a=''/>", limits, 1).error.code, Code::LimitExceeded);
	limits.max_attributes = 1;
	limits.max_attribute_value_bytes = 0;
	EXPECT_EQ(run("<r a=''/>", limits, 1).result, Result::EndDocument);
	EXPECT_EQ(run("<r a='x'/>", limits, 1).error.code, Code::LimitExceeded);
}

TEST(ResourceLimits, RejectUnfinishedTextBeforeRetainingIt)
{
	Limits limits;
	limits.max_element_text_bytes = 4;
	Reader reader(limits);
	reader.input("<r>");
	ASSERT_TRUE(reader.next());
	reader.input("abcd");
	EXPECT_EQ(reader.advance(), Result::NeedInput);
	EXPECT_EQ(reader.statistics().retained_text_bytes, 0u);
	reader.input("e");
	EXPECT_EQ(reader.advance(), Result::Error);
	EXPECT_EQ(reader.error().code, Code::LimitExceeded);
	EXPECT_EQ(reader.error().offset, 7u);
	EXPECT_EQ(reader.statistics().retained_text_bytes, 0u);
	EXPECT_EQ(reader.statistics().retained_text_parts, 0u);
}

TEST(ResourceLimits, RejectPendingAttributesBeforeAllocatingEventValues)
{
	Limits limits;
	limits.max_total_attribute_bytes = 3;
	Reader reader(limits);
	reader.input("<r a='b'><c d='e");
	ASSERT_TRUE(reader.next());
	EXPECT_EQ(reader.advance(), Result::Error);
	EXPECT_EQ(reader.error().code, Code::LimitExceeded);
	EXPECT_EQ(reader.statistics().retained_attribute_bytes, 2u);
	EXPECT_EQ(reader.path(), "/r");
	EXPECT_EQ(reader.depth(), 2);
	EXPECT_EQ(reader.attribute("a", {}), "b");
}

TEST(ResourceLimits, CdataTerminatorBytesDoNotConsumeTextAllowance)
{
	Limits limits;
	limits.max_element_text_bytes = 2;
	limits.max_total_text_bytes = 2;
	EXPECT_EQ(run("<r><![CDATA[ab]]></r>", limits, 1).result, Result::EndDocument);
	EXPECT_EQ(run("<r><![CDATA[]]]]></r>", limits, 1).result, Result::EndDocument);
	EXPECT_EQ(run("<r><![CDATA[abc]]></r>", limits, 1).error.code, Code::LimitExceeded);
	EXPECT_EQ(run("<r><![CDATA[]]x]]></r>", limits, 1).error.code, Code::LimitExceeded);
}

TEST(ResourceLimits, EmptyCdataCannotGrowUnboundedFragmentMetadata)
{
	Limits limits;
	limits.max_text_parts = 3;
	Reader reader(limits);
	reader.input("<r><![CDATA[]]><![CDATA[]]><![CDATA[]]><![CDATA[]]></r>");
	drain(&reader);
	EXPECT_EQ(reader.error().code, Code::LimitExceeded);
	EXPECT_EQ(reader.statistics().retained_text_parts, 3u);
	EXPECT_EQ(reader.statistics().retained_text_bytes, 0u);
}

TEST(ResourceLimits, CommentBodiesAndFragmentsAreNotRetained)
{
	Limits limits;
	limits.max_text_parts = 0;
	limits.max_element_text_bytes = 0;
	limits.max_total_text_bytes = 0;
	Reader reader("<r><!--one--><!--two--></r>", limits);
	size_t comments = 0;
	while (reader.next()) {
		if (reader.state() == Reader::Comment) {
			++comments;
			EXPECT_TRUE(reader.characters().decode().empty());
		}
		EXPECT_EQ(reader.statistics().retained_text_parts, 0u);
		EXPECT_EQ(reader.statistics().retained_text_bytes, 0u);
	}
	EXPECT_EQ(reader.result(), Result::EndDocument);
	EXPECT_EQ(comments, 2u);
}

TEST(ResourceLimits, ClosingElementsReleasesTextPartsAndAttributes)
{
	Limits limits;
	limits.max_total_text_bytes = 3;
	limits.max_text_parts = 1;
	limits.max_total_attribute_bytes = 4;
	Reader reader("<r a='b'><c c='d'>abc</c><c c='d'>def</c></r>", limits);
	size_t children = 0;
	while (reader.next()) {
		if (reader.match_start("/r/c")) {
			EXPECT_EQ(reader.statistics().retained_text_bytes, 0u);
			EXPECT_EQ(reader.statistics().retained_text_parts, 0u);
			EXPECT_EQ(reader.statistics().retained_attribute_bytes, 4u);
		}
		if (reader.match_end("/r/c")) {
			EXPECT_EQ(reader.text(), children++ == 0 ? "abc" : "def");
			EXPECT_EQ(reader.statistics().retained_text_bytes, 3u);
		}
	}
	EXPECT_EQ(reader.result(), Result::EndDocument);
	EXPECT_EQ(children, 2u);
	EXPECT_EQ(reader.statistics().retained_text_bytes, 0u);
	EXPECT_EQ(reader.statistics().retained_text_parts, 0u);
	EXPECT_EQ(reader.statistics().retained_attribute_bytes, 0u);
}

TEST(ResourceLimits, DeclarationsDoNotCountAsAnOpenElement)
{
	Limits limits;
	limits.max_depth = 1;
	EXPECT_EQ(run("<?xml v='1'?><r><?pi a='b'?></r>", limits, 1).result, Result::EndDocument);
	EXPECT_EQ(run("<r><c/></r>", limits, 1).error.code, Code::LimitExceeded);
}

TEST(ResourceLimits, LimitsPersistWhileParseClearsAccountingAndErrors)
{
	Limits limits;
	limits.max_name_bytes = 3;
	Reader reader(limits);
	reader.input("<long/>");
	drain(&reader);
	ASSERT_EQ(reader.error().code, Code::LimitExceeded);
	auto error = reader.error();
	auto before = reader.statistics();
	reader.input("ignored");
	reader.finish_input();
	EXPECT_EQ(reader.advance(), Result::Error);
	EXPECT_EQ(reader.error().message, error.message);
	EXPECT_EQ(reader.statistics().copied_input_bytes, before.copied_input_bytes);
	reader.parse([] {});
	EXPECT_EQ(reader.limits().max_name_bytes, 3u);
	EXPECT_EQ(reader.error().code, Code::None);
	EXPECT_EQ(reader.statistics().scan_steps, 0u);
	EXPECT_EQ(reader.statistics().buffer_capacity, 0u);
	reader.input("<r/>");
	reader.finish_input();
	drain(&reader);
	EXPECT_EQ(reader.result(), Result::EndDocument);
}

TEST(ResourceLimits, ConfigurationCanOnlyChangeBeforeInput)
{
	Reader reader;
	Limits limits;
	limits.max_depth = 1;
	reader.set_limits(limits);
	EXPECT_EQ(reader.limits().max_depth, 1u);
	reader.input("<r/>");
	limits.max_depth = 2;
	reader.set_limits(limits);
	EXPECT_EQ(reader.error().code, Code::InvalidOperation);
	EXPECT_EQ(reader.limits().max_depth, 1u);
	reader.parse([&] { reader.set_limits(limits); });
	EXPECT_EQ(reader.advance(), Result::Error);
	EXPECT_EQ(reader.error().code, Code::InvalidOperation);
}

TEST(ResourceLimits, NestedReaderScopesAreAlsoBounded)
{
	Limits limits;
	limits.max_depth = 2;
	Reader reader("<r/>", limits);
	ASSERT_TRUE(reader.next());
	reader.nest();
	reader.nest();
	reader.nest();
	EXPECT_EQ(reader.error().code, Code::LimitExceeded);
	EXPECT_FALSE(reader.next());
}

TEST(ResourceLimits, LargeConfiguredMaximaDoNotCauseUpfrontAllocation)
{
	Limits limits;
	size_t maximum = std::numeric_limits<size_t>::max();
	limits.max_depth = maximum;
	limits.max_name_bytes = maximum;
	limits.max_attributes = maximum;
	limits.max_attribute_value_bytes = maximum;
	limits.max_token_bytes = maximum;
	limits.max_buffer_bytes = maximum;
	limits.max_element_text_bytes = maximum;
	limits.max_total_text_bytes = maximum;
	limits.max_text_parts = maximum;
	limits.max_total_attribute_bytes = maximum;
	limits.max_path_bytes = maximum;
	limits.max_document_bytes = maximum;
	auto outcome = run("<r a='b'>text</r>", limits, 1);
	EXPECT_EQ(outcome.result, Result::EndDocument);
	EXPECT_LE(outcome.statistics.buffer_capacity, 256u);
}

TEST(IncrementalParsing, LinearWorkForLongCompleteAndUnfinishedTokens)
{
	for (int kind = 0; kind < 7; ++kind) {
		SCOPED_TRACE(kind);
		size_t previous_steps = 0;
		for (size_t size : {size_t(4096), size_t(8192), size_t(16384)}) {
			SCOPED_TRACE(size);
			std::string xml;
			switch (kind) {
			case 0: xml = "<r>" + std::string(size, 'x'); break;
			case 1: xml = "<r><![CDATA[" + std::string(size, 'x'); break;
			case 2: xml = "<r><!--" + std::string(size, 'x'); break;
			case 3: xml = "<" + std::string(size, 'r'); break;
			case 4: xml = "<r" + std::string(size, ' '); break;
			case 5: xml = "<r a='" + std::string(size, 'x'); break;
			case 6:
				xml = "<r";
				for (size_t i = 0; i < size / 16; ++i) xml += " a" + std::to_string(i) + "='value'";
				xml += "/>";
				break;
			}
			Limits limits;
			limits.max_name_bytes = 32768;
			limits.max_attributes = 2048;
			auto outcome = run(xml, limits, 1);
			EXPECT_EQ(outcome.result, kind == 6 ? Result::EndDocument : Result::Error);
			EXPECT_EQ(outcome.error.code, kind == 6 ? Code::None : Code::UnexpectedEof);
			EXPECT_LE(outcome.statistics.scan_steps, xml.size() * 8 + 64);
			EXPECT_LE(outcome.statistics.copied_input_bytes, xml.size() * 3 + 256);
			if (previous_steps != 0) {
				EXPECT_LE(outcome.statistics.scan_steps, previous_steps * 2 + 128);
			}
			previous_steps = outcome.statistics.scan_steps;
		}
	}
}

TEST(IncrementalParsing, WaitingWithoutNewBytesDoesNotRescan)
{
	Reader reader;
	reader.input("<r a='" + std::string(10000, 'x'));
	EXPECT_EQ(reader.advance(), Result::NeedInput);
	auto before = reader.statistics();
	for (int i = 0; i < 100; ++i) EXPECT_EQ(reader.advance(), Result::NeedInput);
	EXPECT_EQ(reader.statistics().scan_steps, before.scan_steps);
	EXPECT_EQ(reader.statistics().copied_input_bytes, before.copied_input_bytes);
	reader.input("'/>");
	reader.finish_input();
	drain(&reader);
	EXPECT_EQ(reader.result(), Result::EndDocument);
}

TEST(IncrementalParsing, RingWrapPreservesAllEventValues)
{
	std::string xml = "<?p a='b'?><r x='&amp;'>hi<![CDATA[x]]><!--abc--><a/></r>";
	Reader borrowed(xml);
	auto expected = events(&borrowed);
	for (size_t capacity : {size_t(16), size_t(17), size_t(31)}) {
		Limits limits;
		limits.max_buffer_bytes = capacity;
		Reader reader(limits);
		size_t pos = 0;
		reader.parse([&] {
			if (pos < xml.size()) reader.input(std::string_view(xml.data() + pos++, 1));
			else reader.finish_input();
		});
		EXPECT_EQ(events(&reader), expected);
		EXPECT_EQ(reader.result(), Result::EndDocument);
		EXPECT_LE(reader.statistics().buffer_capacity, capacity);
	}
}

TEST(IncrementalParsing, ProactiveInputNeverCopiesTheLiveSuffixOnReuse)
{
	Limits limits;
	limits.max_buffer_bytes = 4096;
	Reader reader(limits);
	std::string initial = "<r>";
	for (size_t i = 0; i < 1023; ++i) initial += "<a/>";
	reader.input(initial);
	ASSERT_TRUE(reader.next());
	reader.input("<b/>");
	for (size_t i = 0; i < 10000; ++i) {
		ASSERT_TRUE(reader.next());
		ASSERT_TRUE(reader.is_start_element());
		ASSERT_TRUE(reader.next());
		ASSERT_TRUE(reader.is_end_element());
		reader.input("<b/>");
		ASSERT_NE(reader.state(), Reader::Error);
		EXPECT_LE(reader.statistics().buffer_capacity, limits.max_buffer_bytes);
	}
	drain(&reader);
	ASSERT_EQ(reader.result(), Result::NeedInput);
	reader.input("</r>");
	reader.finish_input();
	drain(&reader);
	EXPECT_EQ(reader.result(), Result::EndDocument);
	size_t received = initial.size() + 4 + 10000 * 4 + 4;
	EXPECT_LE(reader.statistics().copied_input_bytes, received * 3);
	EXPECT_LE(reader.statistics().scan_steps, received * 8);
}

TEST(IncrementalParsing, OneCurrentPathUsesLinearStorageAndAppendWork)
{
	for (size_t depth : {size_t(256), size_t(512)}) {
		Limits limits;
		limits.max_depth = depth;
		limits.max_path_bytes = depth * 2;
		std::string xml;
		for (size_t i = 0; i < depth; ++i) xml += "<r>";
		for (size_t i = 0; i < depth; ++i) xml += "</r>";
		Reader reader(xml, limits);
		size_t maximum_path = 0;
		while (reader.next()) maximum_path = std::max(maximum_path, reader.statistics().path_bytes);
		EXPECT_EQ(reader.result(), Result::EndDocument);
		EXPECT_EQ(maximum_path, depth * 2);
		EXPECT_EQ(reader.statistics().appended_path_bytes, depth * 2);
		EXPECT_EQ(reader.statistics().path_bytes, 0u);
	}
}

TEST(IncrementalParsing, CopyingAnUnfinishedTokenOwnsItsBufferAndScanState)
{
	Reader copy;
	{
		Reader original;
		original.input("<r a='unfinished");
		ASSERT_EQ(original.advance(), Result::NeedInput);
		copy = original;
	}
	copy.input("'/>");
	copy.finish_input();
	ASSERT_TRUE(copy.next());
	EXPECT_EQ(copy.attribute("a", {}), "unfinished");
	drain(&copy);
	EXPECT_EQ(copy.result(), Result::EndDocument);
}
