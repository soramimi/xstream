#include "test.h"

namespace {
using Code = xstream::ErrorCode;
using Error = xstream::ErrorInfo;
using Reader = xstream::Reader;
using Writer = xstream::Writer;
} // namespace

TEST(ErrorStatus, NullOutputPointersReportInvalidOperation)
{
	Error error;
	EXPECT_FALSE(xstream::html_decode("text", static_cast<std::string *>(nullptr), &error));
	EXPECT_EQ(error.code, Code::InvalidOperation);
	EXPECT_EQ(error.offset, 0u);
	EXPECT_FALSE(xstream::html_encode("text", static_cast<std::string *>(nullptr), &error));
	EXPECT_EQ(error.code, Code::InvalidOperation);
	Reader reader("<r a='value'>text</r>");
	ASSERT_TRUE(reader.next());
	auto attribute = reader.attribute("a");
	ASSERT_TRUE(attribute);
	EXPECT_FALSE(attribute->to_string(nullptr, &error));
	EXPECT_EQ(error.code, Code::InvalidOperation);
	ASSERT_TRUE(reader.next());
	EXPECT_FALSE(reader.characters().decode(nullptr, &error));
	EXPECT_EQ(error.code, Code::InvalidOperation);
	EXPECT_EQ(error.offset, 0u);
}

TEST(ErrorStatus, DecodeReportsCodesAndPositionsWithoutChangingOutput)
{
	struct Case { std::string input; Code code; size_t offset; };
	for (auto const &test : std::vector<Case>{
		{"ok&unknown;", Code::InvalidCharacterReference, 2},
		{"ok&amp", Code::InvalidCharacterReference, 2},
		{"a&#0;", Code::InvalidCharacterReference, 1},
		{"ok\x80", Code::InvalidUtf8, 2},
		{"ok\xf0\x9f\x98", Code::InvalidUtf8, 2},
		{"ok\x01", Code::InvalidCharacter, 2},
		{std::string("a\0b", 3), Code::InvalidCharacter, 1}}) {
		std::string output = "keep";
		Error error;
		EXPECT_FALSE(xstream::html_decode(test.input, &output, &error));
		EXPECT_EQ(output, "keep");
		EXPECT_EQ(error.code, test.code);
		EXPECT_EQ(error.offset, test.offset);
		EXPECT_FALSE(error.message.empty());
		EXPECT_TRUE(xstream::html_decode(test.input, &error).empty());
		EXPECT_EQ(error.code, test.code);
		EXPECT_EQ(error.offset, test.offset);
	}
}

TEST(ErrorStatus, EncodeReportsCodesAndPositionsWithoutChangingOutput)
{
	for (bool utf8through : {false, true}) {
		for (std::string const input : {"ok\x80", "ok\x01", "ok\xed\xa0\x80"}) {
			std::string output = "keep";
			Error error;
			EXPECT_FALSE(xstream::html_encode(input, &output, &error, utf8through));
			EXPECT_EQ(output, "keep");
			EXPECT_EQ(error.code, input[2] == '\x01' ? Code::InvalidCharacter : Code::InvalidUtf8);
			EXPECT_EQ(error.offset, 2u);
			EXPECT_FALSE(error.message.empty());
			EXPECT_TRUE(xstream::html_encode(input, utf8through, &error).empty());
			EXPECT_NE(error.code, Code::None);
		}
	}
}

TEST(ErrorStatus, SuccessfulAndEmptyConversionsClearDiagnosticsAndSupportAliasing)
{
	std::string value = "&amp;&#x1F600;";
	Error error{Code::InputError, 100, "old"};
	ASSERT_TRUE(xstream::html_decode(value, &value, &error));
	EXPECT_EQ(value, "&\xf0\x9f\x98\x80");
	EXPECT_EQ(error.code, Code::None);
	EXPECT_EQ(error.offset, 0u);
	EXPECT_TRUE(error.message.empty());
	ASSERT_TRUE(xstream::html_encode(value, &value, &error, false));
	EXPECT_EQ(value, "&amp;&#128512;");
	ASSERT_TRUE(xstream::html_decode({}, &value, &error));
	EXPECT_TRUE(value.empty());
	error = {Code::InvalidSyntax, 3, "old"};
	ASSERT_TRUE(xstream::html_encode({}, &value, &error));
	EXPECT_TRUE(value.empty());
	EXPECT_EQ(error.code, Code::None);
	EXPECT_EQ(xstream::html_encode("&", true, &error), "&amp;");
	EXPECT_EQ(error.code, Code::None);
	EXPECT_TRUE(xstream::html_encode("\x80", &error).empty());
	EXPECT_EQ(error.code, Code::InvalidUtf8);
	EXPECT_TRUE(xstream::html_decode("&bad;").empty());
	EXPECT_TRUE(xstream::html_encode("\x80").empty());
}

TEST(ErrorStatus, CheckedReaderValuesReturnSuccess)
{
	Reader reader("<r a='&amp;'>text&#x1F600;<![CDATA[<raw>]]><!--comment--></r>");
	Error error;
	size_t parts = 0;
	while (reader.next()) {
		if (auto attribute = reader.attribute("a")) {
			std::string value;
			ASSERT_TRUE(attribute->to_string(&value, &error));
			EXPECT_EQ(value, "&");
		}
		if (reader.is_characters() || reader.state() == Reader::Comment) {
			auto part = reader.characters();
			std::vector<char> value;
			ASSERT_TRUE(part.decode(&value, &error));
			EXPECT_EQ(value, part.decode());
			++parts;
		}
	}
	EXPECT_EQ(parts, 3u);
	EXPECT_EQ(reader.result(), Reader::ReadResult::EndDocument);
	EXPECT_EQ(error.code, Code::None);
}

TEST(ErrorStatus, WriterConversionFailureStopsFurtherOperations)
{
	for (bool attribute : {false, true}) {
		int calls = 0;
		Writer writer([&](char const *, int count) { ++calls; return count; });
		ASSERT_TRUE(writer.start_element("r"));
		bool success = attribute ? writer.write_attribute("a", "ok\x01") : writer.write_characters("ok\x80");
		EXPECT_FALSE(success);
		EXPECT_FALSE(writer.good());
		Error error = writer.error();
		EXPECT_EQ(error.code, attribute ? Code::InvalidCharacter : Code::InvalidUtf8);
		EXPECT_EQ(error.offset, 2u);
		EXPECT_FALSE(writer.write_characters("later"));
		EXPECT_FALSE(writer.write_attribute("a", "later"));
		EXPECT_FALSE(writer.start_document());
		EXPECT_FALSE(writer.start_element("x"));
		EXPECT_FALSE(writer.end_element());
		EXPECT_FALSE(writer.end_document());
		writer.output_error("later output failure");
		EXPECT_EQ(writer.error().code, error.code);
		EXPECT_EQ(writer.error().offset, error.offset);
		EXPECT_EQ(writer.error().message, error.message);
		EXPECT_EQ(calls, 0);
	}
}

TEST(ErrorStatus, WriterHandlesPartialOutputAndFinishesValidXml)
{
	std::string xml;
	Writer writer([&](char const *p, int count) {
		int size = std::min(count, 2);
		xml.append(p, size_t(size));
		return size;
	});
	ASSERT_TRUE(writer.start_element("r"));
	ASSERT_TRUE(writer.write_attribute("a", "&"));
	ASSERT_TRUE(writer.write_characters("x"));
	ASSERT_TRUE(writer.end_document());
	EXPECT_TRUE(writer.good());
	EXPECT_EQ(writer.error().code, Code::None);
	EXPECT_EQ(xml, "<r a=\"&amp;\">x</r>\n");
	Reader reader(xml);
	while (reader.next()) {}
	EXPECT_EQ(reader.result(), Reader::ReadResult::EndDocument);
}

TEST(ErrorStatus, WriterRejectsFailedAndInvalidOutputCounts)
{
	for (int result : {-1, 0, 100}) {
		int calls = 0;
		Writer writer([&](char const *, int) { ++calls; return result; });
		ASSERT_TRUE(writer.start_element("r"));
		EXPECT_FALSE(writer.end_document());
		EXPECT_EQ(writer.error().code, Code::OutputError);
		EXPECT_EQ(writer.error().offset, 0u);
		EXPECT_FALSE(writer.end_document());
		EXPECT_EQ(calls, 1);
	}
}

TEST(ErrorStatus, WriterReportsTheFirstUnwrittenByteAfterPartialOutput)
{
	std::string output;
	int calls = 0;
	Writer writer([&](char const *p, int) {
		if (++calls == 1) { output.append(p, 2); return 2; }
		return -1;
	});
	ASSERT_TRUE(writer.start_element("r"));
	EXPECT_FALSE(writer.end_document());
	EXPECT_EQ(output, "<r");
	EXPECT_EQ(writer.error().code, Code::OutputError);
	EXPECT_EQ(writer.error().offset, 2u);
	EXPECT_FALSE(writer.end_document());
	EXPECT_EQ(calls, 2);
}

TEST(ErrorStatus, WriterReportsFailureOnTheFinalNewline)
{
	std::string output;
	Writer writer([&](char const *p, int n) {
		if (n == 1 && *p == '\n') return 0;
		output.append(p, size_t(n));
		return n;
	});
	ASSERT_TRUE(writer.start_element("r"));
	EXPECT_FALSE(writer.end_document());
	EXPECT_EQ(output, "<r />");
	EXPECT_EQ(writer.error().code, Code::OutputError);
	EXPECT_EQ(writer.error().offset, 5u);
}

TEST(ErrorStatus, WriterReportsMissingCallbackAndMisplacedAttributes)
{
	Writer missing(std::function<int (char const *, int)>{});
	EXPECT_FALSE(missing.good());
	EXPECT_FALSE(missing.start_document());
	EXPECT_EQ(missing.error().code, Code::InvalidOperation);
	int calls = 0;
	Writer writer([&](char const *, int n) { ++calls; return n; });
	EXPECT_FALSE(writer.write_attribute("a", "value"));
	EXPECT_EQ(writer.error().code, Code::InvalidOperation);
	EXPECT_FALSE(writer.end_document());
	EXPECT_EQ(calls, 0);
}

TEST(ErrorStatus, WriterCallbackCanReportAnErrorAndCannotReenterWriting)
{
	for (bool reenter : {false, true}) {
		Writer *active = nullptr;
		int calls = 0;
		Writer writer([&](char const *, int n) {
			++calls;
			if (reenter) { EXPECT_FALSE(active->start_element("recursive")); }
			else { active->output_error("sink disconnected"); }
			return n;
		});
		active = &writer;
		ASSERT_TRUE(writer.start_element("r"));
		EXPECT_FALSE(writer.end_document());
		EXPECT_EQ(writer.error().code, reenter ? Code::InvalidOperation : Code::OutputError);
		if (!reenter) { EXPECT_EQ(writer.error().message, "sink disconnected"); }
		EXPECT_FALSE(writer.end_document());
		EXPECT_EQ(calls, 1);
	}
}

TEST(ErrorStatus, WriterConvenienceFunctionsStopAfterAnError)
{
	int calls = 0;
	Writer writer([&](char const *, int n) { ++calls; return n; });
	EXPECT_FALSE(writer.element("r", [&] { EXPECT_FALSE(writer.write_characters("\x80")); }));
	bool invoked = false;
	EXPECT_FALSE(writer.element("x", [&] { invoked = true; }));
	EXPECT_FALSE(writer.text_element("x", "text"));
	EXPECT_FALSE(invoked);
	EXPECT_EQ(calls, 0);
}
