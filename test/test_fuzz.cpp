#include "test.h"
#include "fuzz/reader_fuzz.h"

namespace {
std::string input(std::string_view xml)
{
	return std::string(reader_fuzz::header_size, '4') + std::string(xml);
}
} // namespace

TEST(Fuzzing, SeedDocumentsAcrossAllLimitProfiles)
{
	for (std::string const xml : {"", "<r/>", "<?xml version='1.0'?><r a='&amp;'>text<x/></r>",
		"<r><![CDATA[]]><![CDATA[&raw]]><!--comment--></r>", "<r a='1' a='2'/>",
		"<r><x></r>", "<r>&bad;</r>", "<r>\xf0\x9f\x98", "<!DOCTYPE r>",
		"<r><!--a--b---></r>", "\xef\xbb\xbf<r>&#x1F600;</r>",
		" \t<!---->\n<?pi?>\r<r/>\n<!---->\t"}) {
		SCOPED_TRACE(xml);
		for (size_t field = 0; field < reader_fuzz::header_size; ++field) {
			for (unsigned char profile = '0'; profile <= '4'; ++profile) {
				SCOPED_TRACE(field);
				SCOPED_TRACE(profile);
				auto bytes = input(xml);
				bytes[field] = char(profile);
				EXPECT_NO_THROW(reader_fuzz::check_input(bytes));
			}
		}
	}
}

TEST(Fuzzing, DeterministicByteMutations)
{
	std::string original = input("<?xml version='1.0'?><r a='&#x1F600;'>text<!--comment--><![CDATA[raw]]><x/></r>");
	for (size_t position = 0; position < original.size(); ++position) {
		for (unsigned char value : {0u, 0x80u, 0xffu, unsigned('<'), unsigned('>'), unsigned('&'), unsigned(';'), unsigned('\''), unsigned('"')}) {
			SCOPED_TRACE(position);
			SCOPED_TRACE(unsigned(value));
			auto bytes = original;
			bytes[position] = char(value);
			EXPECT_NO_THROW(reader_fuzz::check_input(bytes));
		}
	}
}

TEST(Fuzzing, TruncationAtEveryInputByte)
{
	std::string bytes = input("\xef\xbb\xbf<r a='&#x1F600;'>\xc3\xa9\xf0\x9f\x98\x80&amp;<x/></r>");
	for (size_t length = 0; length <= bytes.size(); ++length) {
		SCOPED_TRACE(length);
		EXPECT_NO_THROW(reader_fuzz::check_input(std::string_view(bytes).substr(0, length)));
	}
}

TEST(Fuzzing, LongAndDeepBoundaryInputs)
{
	std::string deep;
	for (int i = 0; i < 128; ++i) deep += "<r a='1'>";
	for (int i = 0; i < 128; ++i) deep += "</r>";
	for (std::string const &xml : {deep, "<r>" + std::string(2048, 'x') + "</r>",
		"<r>&#" + std::string(2048, '9') + ";</r>", "<r><!--" + std::string(2048, 'x')}) {
		for (char mode : {'0', '1', '2', '3'}) {
			auto bytes = input(xml);
			bytes[11] = mode;
			EXPECT_NO_THROW(reader_fuzz::check_input(bytes));
		}
	}
}
