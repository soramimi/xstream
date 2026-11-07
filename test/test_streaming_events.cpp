#include "test.h"
#include <gtest/gtest.h>
#include <tuple>

namespace {

using Reader = xstream::Reader;
using Event = std::tuple<Reader::StateType, std::string, std::string,
	std::vector<std::pair<std::string, std::string>>, std::string>;

std::vector<Event> collect_events(Reader *reader)
{
	std::vector<Event> events;
	while (reader->next()) {
		if (events.size() >= 1000) {
			ADD_FAILURE() << "Reader did not stop";
			break;
		}
		EXPECT_NE(reader->state(), Reader::None);
		EXPECT_EQ(reader->is_start_element(), reader->state() == Reader::StartElement);
		EXPECT_EQ(reader->is_end_element(), reader->state() == Reader::EndElement);
		EXPECT_EQ(reader->is_characters(), reader->state() == Reader::Characters);
		EXPECT_EQ(reader->is_declaration(), reader->state() == Reader::Declaration);
		EXPECT_EQ(reader->match_start(reader->path().c_str()),
			reader->is_start_element() || reader->is_declaration());
		EXPECT_EQ(reader->match_end(reader->path().c_str()), reader->is_end_element());
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
		events.emplace_back(reader->state(), reader->name(), reader->path(), attributes, text);
	}
	return events;
}

std::vector<Event> stream_events(std::vector<std::string> const &chunks)
{
	Reader reader;
	size_t chunk = 0;
	reader.parse([&] {
		if (chunk < chunks.size()) reader.input(chunks[chunk++]);
		else reader.finish_input();
	});
	auto events = collect_events(&reader);
	EXPECT_EQ(chunk, chunks.size());
	EXPECT_EQ(reader.result(), Reader::ReadResult::EndDocument);
	return events;
}

} // namespace

TEST(StreamingEvents, CallbackRefillReturnsOnlyCompletedEvents)
{
	auto events = stream_events({"<root>", "<child>", "text", "</child>", "</root>"});
	ASSERT_EQ(events.size(), 5u);
	EXPECT_EQ(std::get<0>(events[0]), Reader::StartElement);
	EXPECT_EQ(std::get<1>(events[0]), "root");
	EXPECT_EQ(std::get<0>(events[1]), Reader::StartElement);
	EXPECT_EQ(std::get<1>(events[1]), "child");
	EXPECT_EQ(std::get<0>(events[2]), Reader::Characters);
	EXPECT_EQ(std::get<4>(events[2]), "text");
	EXPECT_EQ(std::get<0>(events[3]), Reader::EndElement);
	EXPECT_EQ(std::get<1>(events[3]), "child");
	EXPECT_EQ(std::get<0>(events[4]), Reader::EndElement);
	EXPECT_EQ(std::get<1>(events[4]), "root");
}

TEST(StreamingEvents, PartialTokenDoesNotPublishNameAttributesOrDepth)
{
	Reader reader;
	reader.parse([] {});
	reader.input("<root attr='old'>");
	ASSERT_TRUE(reader.next());
	reader.input("<child attr='new");
	EXPECT_FALSE(reader.next());
	EXPECT_TRUE(reader.is_not_enough_input());
	EXPECT_EQ(reader.state(), Reader::None);
	EXPECT_FALSE(reader.is_start_element());
	EXPECT_FALSE(reader.is_end_element());
	EXPECT_FALSE(reader.match_start("/root"));
	EXPECT_EQ(reader.name(), "root");
	EXPECT_EQ(reader.attribute("attr", {}), "old");
	EXPECT_EQ(reader.path(), "/root");
	EXPECT_EQ(reader.depth(), 2);
	reader.input("'/></root>");
	auto events = collect_events(&reader);
	ASSERT_EQ(events.size(), 3u);
	EXPECT_EQ(std::get<0>(events[0]), Reader::StartElement);
	EXPECT_EQ(std::get<1>(events[0]), "child");
	ASSERT_EQ(std::get<3>(events[0]).size(), 1u);
	EXPECT_EQ(std::get<3>(events[0])[0].second, "new");
	EXPECT_EQ(std::get<0>(events[1]), Reader::EndElement);
	EXPECT_EQ(std::get<1>(events[1]), "child");
	EXPECT_EQ(std::get<1>(events[2]), "root");
}

TEST(StreamingEvents, EmptyElementWaitsForClosingBracket)
{
	Reader reader;
	reader.parse([] {});
	reader.input("<root><item/");
	ASSERT_TRUE(reader.next());
	ASSERT_TRUE(reader.match_start("/root"));
	EXPECT_FALSE(reader.next());
	EXPECT_FALSE(reader.next());
	EXPECT_EQ(reader.state(), Reader::None);
	EXPECT_EQ(reader.name(), "root");
	EXPECT_EQ(reader.depth(), 2);
	reader.input("></root>");
	auto events = collect_events(&reader);
	ASSERT_EQ(events.size(), 3u);
	EXPECT_EQ(std::get<0>(events[0]), Reader::StartElement);
	EXPECT_EQ(std::get<2>(events[0]), "/root/item");
	EXPECT_EQ(std::get<0>(events[1]), Reader::EndElement);
	EXPECT_EQ(std::get<2>(events[1]), "/root/item");
	EXPECT_EQ(std::get<0>(events[2]), Reader::EndElement);
	EXPECT_EQ(std::get<2>(events[2]), "/root");
}

TEST(StreamingEvents, EmptyRootHasMatchingStartAndEnd)
{
	auto events = stream_events({"<root/", ">"});
	ASSERT_EQ(events.size(), 2u);
	EXPECT_EQ(std::get<0>(events[0]), Reader::StartElement);
	EXPECT_EQ(std::get<0>(events[1]), Reader::EndElement);
	EXPECT_EQ(std::get<2>(events[0]), "/root");
	EXPECT_EQ(std::get<2>(events[1]), "/root");
}

TEST(StreamingEvents, AllSplitPositionsMatchOneShotEvents)
{
	std::string xml = "<?xml version='1.0'?><root attr='&amp;'>before<!--comment-->"
		"<![CDATA[<raw>]]><item/><item attr='value' />after</root>";
	Reader one_shot(xml);
	auto expected = collect_events(&one_shot);
	ASSERT_EQ(expected.size(), 11u);
	for (size_t split = 1; split < xml.size(); ++split) {
		SCOPED_TRACE(split);
		EXPECT_EQ(stream_events({xml.substr(0, split), xml.substr(split)}), expected);
	}
	std::vector<std::string> bytes;
	for (char c : xml) bytes.emplace_back(1, c);
	EXPECT_EQ(stream_events(bytes), expected);
}

TEST(StreamingEvents, NoProgressReturnsWithoutRepeatingAnEvent)
{
	Reader reader;
	size_t calls = 0;
	reader.parse([&] {
		if (++calls == 1) reader.input("<child");
	});
	reader.input("<root>");
	ASSERT_TRUE(reader.next());
	ASSERT_TRUE(reader.match_start("/root"));
	EXPECT_FALSE(reader.next());
	EXPECT_EQ(calls, 2u);
	EXPECT_EQ(reader.state(), Reader::None);
	reader.input("/></root>");
	auto events = collect_events(&reader);
	ASSERT_EQ(events.size(), 3u);
	EXPECT_EQ(std::get<0>(events[0]), Reader::StartElement);
	EXPECT_EQ(std::get<1>(events[0]), "child");
	EXPECT_EQ(std::get<0>(events[1]), Reader::EndElement);
	EXPECT_EQ(std::get<1>(events[2]), "root");
	EXPECT_FALSE(reader.next());
	EXPECT_EQ(reader.state(), Reader::None);
}

TEST(StreamingEvents, EmptyCallbackAndEmptyInputDoNotCreateEvents)
{
	Reader reader;
	size_t calls = 0;
	reader.parse([&] { ++calls; reader.input({}); });
	EXPECT_FALSE(reader.next());
	EXPECT_EQ(calls, 1u);
	EXPECT_EQ(reader.state(), Reader::None);
	reader.hold();
	EXPECT_FALSE(reader.next());
	EXPECT_EQ(calls, 2u);
}

TEST(StreamingEvents, StreamingNestPreservesParentEndForOuterLoop)
{
	std::string xml = "<root><branch><leaf/></branch><sibling/></root>";
	size_t pos = 0;
	Reader reader;
	reader.parse([&] {
		if (pos < xml.size()) reader.input(std::string_view(xml.data() + pos++, 1));
		else reader.finish_input();
	});
	ASSERT_TRUE(reader.next());
	ASSERT_TRUE(reader.match_start("/root"));
	ASSERT_TRUE(reader.next());
	ASSERT_TRUE(reader.match_start("/root/branch"));
	reader.nest();
	ASSERT_TRUE(reader.next());
	EXPECT_TRUE(reader.match_start("/root/branch/leaf"));
	ASSERT_TRUE(reader.next());
	EXPECT_TRUE(reader.match_end("/root/branch/leaf"));
	EXPECT_FALSE(reader.next()); // branch end is held for the outer loop.
	ASSERT_TRUE(reader.next());
	EXPECT_TRUE(reader.match_end("/root/branch"));
	ASSERT_TRUE(reader.next());
	EXPECT_TRUE(reader.match_start("/root/sibling"));
	ASSERT_TRUE(reader.next());
	EXPECT_TRUE(reader.match_end("/root/sibling"));
	ASSERT_TRUE(reader.next());
	EXPECT_TRUE(reader.match_end("/root"));
	EXPECT_FALSE(reader.next());
	EXPECT_EQ(reader.result(), Reader::ReadResult::EndDocument);
}
