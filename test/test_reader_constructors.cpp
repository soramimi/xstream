#include "test.h"
#include <type_traits>

namespace {
using Reader = xstream::Reader;

template<class Source>
constexpr bool supports_borrowed_source = std::is_constructible_v<Reader, Source>
	&& std::is_constructible_v<Reader, Source, Reader::Limits const &>;

template<class Source>
constexpr bool rejects_temporary_string = !std::is_constructible_v<Reader, Source>
	&& !std::is_constructible_v<Reader, Source, Reader::Limits const &>;

static_assert(supports_borrowed_source<decltype("<r/>")>);
static_assert(supports_borrowed_source<char const *>);
static_assert(supports_borrowed_source<char *>);
static_assert(supports_borrowed_source<std::string &>);
static_assert(supports_borrowed_source<std::string const &>);
static_assert(supports_borrowed_source<std::string_view>);
static_assert(supports_borrowed_source<std::string_view const &>);
static_assert(rejects_temporary_string<std::string>);
static_assert(rejects_temporary_string<std::string const>);
static_assert(rejects_temporary_string<std::string &&>);
static_assert(rejects_temporary_string<std::string const &&>);
static_assert(std::is_constructible_v<Reader, char const *, char const *>);
static_assert(std::is_constructible_v<Reader, char const *, size_t>);

void expect_document(Reader *reader)
{
	ASSERT_TRUE(reader->next());
	ASSERT_TRUE(reader->is_start_element("r"));
	EXPECT_EQ(reader->attribute("a", ""), "1");
	ASSERT_TRUE(reader->next());
	ASSERT_TRUE(reader->is_characters());
	auto chars = reader->characters().decode();
	EXPECT_EQ(std::string(chars.begin(), chars.end()), "text");
	ASSERT_TRUE(reader->next());
	ASSERT_TRUE(reader->is_end_element("r"));
	EXPECT_EQ(reader->text(), "text");
	EXPECT_FALSE(reader->next());
	EXPECT_EQ(reader->result(), Reader::ReadResult::EndDocument);
	EXPECT_EQ(reader->error().code, Reader::ErrorCode::None);
}
} // namespace

TEST(ReaderConstructors, StableBorrowedSourcesProduceCompleteEvents)
{
	std::string xml = "<r a='1'>text</r>";
	std::string const constant_xml = xml;
	std::string_view view = xml;
	char mutable_xml[] = "<r a='1'>text</r>";
	char const *pointer = xml.c_str();
	for (auto reader : std::vector<Reader>{
		Reader("<r a='1'>text</r>"), Reader(pointer), Reader(mutable_xml),
		Reader(xml), Reader(constant_xml), Reader(view),
		Reader(pointer, pointer + xml.size()), Reader(pointer, xml.size())}) {
		expect_document(&reader);
	}
}

TEST(ReaderConstructors, StableBorrowedSourcesPreserveConfiguredLimits)
{
	std::string xml = "<r a='1'>text</r>";
	std::string const constant_xml = xml;
	std::string_view view = xml;
	char mutable_xml[] = "<r a='1'>text</r>";
	char const *pointer = xml.c_str();
	Reader::Limits limits;
	limits.max_depth = 1;
	for (auto reader : std::vector<Reader>{
		Reader("<r a='1'>text</r>", limits), Reader(pointer, limits), Reader(mutable_xml, limits),
		Reader(xml, limits), Reader(constant_xml, limits), Reader(view, limits)}) {
		EXPECT_EQ(reader.limits().max_depth, limits.max_depth);
		expect_document(&reader);
	}
}
