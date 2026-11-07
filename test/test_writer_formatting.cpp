#include "test.h"
#include <tuple>

namespace {
using Reader = xstream::Reader;
using Writer = xstream::Writer;
using Code = xstream::ErrorCode;
using Element = std::tuple<std::string, std::string>;

void expect_elements(std::string const &xml, std::vector<Element> const &expected)
{
	Reader reader(xml);
	std::vector<Element> elements;
	std::vector<std::string> open;
	size_t starts = 0;
	while (reader.next()) {
		ASSERT_LT(starts + elements.size(), xml.size() * 2 + 16);
		if (reader.is_start_element()) {
			open.push_back(reader.path());
			++starts;
		} else if (reader.is_end_element()) {
			ASSERT_FALSE(open.empty());
			EXPECT_EQ(reader.path(), open.back());
			open.pop_back();
			elements.emplace_back(reader.path(), reader.text());
		}
	}
	EXPECT_EQ(elements, expected);
	EXPECT_EQ(starts, expected.size());
	EXPECT_TRUE(open.empty());
	EXPECT_EQ(reader.result(), Reader::ReadResult::EndDocument) << reader.error().message;
	EXPECT_EQ(reader.error().code, Code::None);
}
} // namespace

TEST(WriterFormatting, TextBeforeChildrenDoesNotGainWhitespace)
{
	std::string xml;
	Writer writer([&](char const *p, int n) { xml.append(p, size_t(n)); return n; });
	ASSERT_TRUE(writer.start_element("a"));
	ASSERT_TRUE(writer.write_characters("x"));
	ASSERT_TRUE(writer.start_element("b"));
	ASSERT_TRUE(writer.end_element());
	ASSERT_TRUE(writer.start_element("c"));
	ASSERT_TRUE(writer.end_element());
	ASSERT_TRUE(writer.write_characters("y"));
	ASSERT_TRUE(writer.start_element("d"));
	ASSERT_TRUE(writer.end_document());
	EXPECT_EQ(xml, "<a>x<b /><c />y<d /></a>\n");
	expect_elements(xml, {{"/a/b", ""}, {"/a/c", ""}, {"/a/d", ""}, {"/a", "xy"}});
}

TEST(WriterFormatting, AttributeWhitespaceUsesCharacterReferences)
{
	std::string xml;
	Writer writer([&](char const *p, int n) { xml.append(p, size_t(n)); return n; });
	std::string value = "\t\n\r\r\n \xc3\xa9&<>\"'";
	ASSERT_TRUE(writer.start_element("r"));
	ASSERT_TRUE(writer.write_attribute("a", value));
	ASSERT_TRUE(writer.end_document());
	EXPECT_EQ(xml, "<r a=\"&#9;&#10;&#13;&#13;&#10; \xc3\xa9&amp;&lt;&gt;&quot;&apos;\" />\n");
	Reader reader(xml);
	ASSERT_TRUE(reader.next());
	ASSERT_TRUE(reader.is_start_element("r"));
	EXPECT_EQ(reader.attribute("a", ""), value);
	ASSERT_TRUE(reader.next());
	ASSERT_TRUE(reader.is_end_element("r"));
	ASSERT_TRUE(reader.next());
	ASSERT_TRUE(reader.is_characters());
	auto trailing = reader.characters().decode();
	EXPECT_EQ(std::string(trailing.begin(), trailing.end()), "\n");
	EXPECT_FALSE(reader.next());
	EXPECT_EQ(reader.result(), Reader::ReadResult::EndDocument);
}

TEST(WriterFormatting, MixedParentAndElementOnlyChildKeepTheirOwnFormatting)
{
	std::string xml;
	Writer writer([&](char const *p, int n) { xml.append(p, size_t(n)); return n; });
	ASSERT_TRUE(writer.start_element("r"));
	ASSERT_TRUE(writer.start_element("a"));
	ASSERT_TRUE(writer.write_characters("x"));
	ASSERT_TRUE(writer.start_element("b"));
	ASSERT_TRUE(writer.start_element("z"));
	ASSERT_TRUE(writer.end_element());
	ASSERT_TRUE(writer.end_element());
	ASSERT_TRUE(writer.text_element("c", "y"));
	ASSERT_TRUE(writer.end_element());
	ASSERT_TRUE(writer.start_element("d"));
	ASSERT_TRUE(writer.end_document());
	EXPECT_EQ(xml, "<r>\n    <a>x<b>\n            <z />\n        </b><c>y</c></a>\n    <d />\n</r>\n");
	expect_elements(xml, {{"/r/a/b/z", ""}, {"/r/a/b", "\n            \n        "},
		{"/r/a/c", "y"}, {"/r/a", "x"}, {"/r/d", ""}, {"/r", "\n    \n    \n"}});
}

TEST(WriterFormatting, EmptyTextSuppressesLaterFormattingInItsElement)
{
	std::string xml;
	Writer writer([&](char const *p, int n) { xml.append(p, size_t(n)); return n; });
	ASSERT_TRUE(writer.start_element("a"));
	ASSERT_TRUE(writer.write_characters(""));
	ASSERT_TRUE(writer.element("b", {}));
	ASSERT_TRUE(writer.element("c", {}));
	ASSERT_TRUE(writer.end_document());
	EXPECT_EQ(xml, "<a><b /><c /></a>\n");
	expect_elements(xml, {{"/a/b", ""}, {"/a/c", ""}, {"/a", ""}});
}

TEST(WriterFormatting, AutomaticFormattingCannotUndoEarlierWhitespace)
{
	std::string xml;
	Writer writer([&](char const *p, int n) { xml.append(p, size_t(n)); return n; });
	ASSERT_TRUE(writer.start_element("a"));
	ASSERT_TRUE(writer.element("b", {}));
	ASSERT_TRUE(writer.write_characters("x"));
	ASSERT_TRUE(writer.element("c", {}));
	ASSERT_TRUE(writer.end_document());
	EXPECT_EQ(xml, "<a>\n    <b />x<c /></a>\n");
	expect_elements(xml, {{"/a/b", ""}, {"/a/c", ""}, {"/a", "\n    x"}});
}

TEST(WriterFormatting, DefaultElementOnlyOutputIsPreserved)
{
	std::string xml;
	Writer writer([&](char const *p, int n) { xml.append(p, size_t(n)); return n; });
	EXPECT_EQ(writer.indent_width(), 4u);
	ASSERT_TRUE(writer.start_document());
	ASSERT_TRUE(writer.start_element("r"));
	ASSERT_TRUE(writer.element("empty", {}));
	ASSERT_TRUE(writer.text_element("text", "v"));
	ASSERT_TRUE(writer.start_element("c"));
	ASSERT_TRUE(writer.element("z", {}));
	ASSERT_TRUE(writer.end_document());
	EXPECT_EQ(xml, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<r>\n    <empty />\n    <text>v</text>\n    <c>\n        <z />\n    </c>\n</r>\n");
	expect_elements(xml, {{"/r/empty", ""}, {"/r/text", "v"}, {"/r/c/z", ""},
		{"/r/c", "\n        \n    "}, {"/r", "\n    \n    \n    \n"}});
}

TEST(WriterFormatting, IndentWidthsControlAllAutomaticWhitespace)
{
	for (size_t width : {size_t(0), size_t(1), size_t(2), size_t(4), Writer::max_indent_width}) {
		for (bool declaration : {false, true}) {
			SCOPED_TRACE(width);
			SCOPED_TRACE(declaration);
			std::string xml;
			Writer writer([&](char const *p, int n) { xml.append(p, size_t(n)); return n; });
			ASSERT_TRUE(writer.set_indent_width(width));
			EXPECT_EQ(writer.indent_width(), width);
			if (declaration) { ASSERT_TRUE(writer.start_document()); }
			ASSERT_TRUE(writer.start_element("r"));
			ASSERT_TRUE(writer.element("b", {}));
			ASSERT_TRUE(writer.start_element("c"));
			ASSERT_TRUE(writer.element("d", {}));
			ASSERT_TRUE(writer.end_document());
			std::string first = width == 0 ? "" : "\n" + std::string(width, ' ');
			std::string second = width == 0 ? "" : "\n" + std::string(width * 2, ' ');
			std::string newline = width == 0 ? "" : "\n";
			std::string prefix = declaration ? "<?xml version=\"1.0\" encoding=\"UTF-8\"?>" + newline : "";
			EXPECT_EQ(xml, prefix + "<r>" + first + "<b />" + first + "<c>" + second + "<d />" + first + "</c>" + newline + "</r>" + newline);
			expect_elements(xml, {{"/r/b", ""}, {"/r/c/d", ""}, {"/r/c", second + first}, {"/r", first + first + newline}});
		}
	}
}

TEST(WriterFormatting, CompactMixedContentPreservesTextWithPartialWrites)
{
	for (bool declaration : {false, true}) {
		for (bool text_first : {false, true}) {
			std::string xml;
			Writer writer([&](char const *p, int n) {
				int count = std::min(n, 2);
				xml.append(p, size_t(count));
				return count;
			});
			ASSERT_TRUE(writer.set_indent_width(0));
			if (declaration) { ASSERT_TRUE(writer.start_document()); }
			ASSERT_TRUE(writer.start_element("a"));
			ASSERT_TRUE(writer.write_attribute("v", "\t\n\r"));
			if (text_first) { ASSERT_TRUE(writer.write_characters("x")); }
			ASSERT_TRUE(writer.element("b", {}));
			ASSERT_TRUE(writer.write_characters(text_first ? "y\t \n" : "x\t \n"));
			ASSERT_TRUE(writer.end_document());
			std::string prefix = declaration ? "<?xml version=\"1.0\" encoding=\"UTF-8\"?>" : "";
			EXPECT_EQ(xml, prefix + "<a v=\"&#9;&#10;&#13;\">" + (text_first ? "x" : "") + "<b />" + (text_first ? "y" : "x") + "\t \n</a>");
			expect_elements(xml, {{"/a/b", ""}, {"/a", text_first ? "xy\t \n" : "x\t \n"}});
			EXPECT_TRUE(writer.good());
		}
	}
}

TEST(WriterFormatting, SettingsCanChangeRepeatedlyBeforeAnyOutputOperation)
{
	std::string xml;
	Writer writer([&](char const *p, int n) { xml.append(p, size_t(n)); return n; });
	ASSERT_TRUE(writer.set_indent_width(Writer::max_indent_width));
	ASSERT_TRUE(writer.set_indent_width(2));
	ASSERT_TRUE(writer.set_indent_width(0));
	EXPECT_TRUE(xml.empty());
	ASSERT_TRUE(writer.start_element("r"));
	ASSERT_TRUE(writer.end_document());
	EXPECT_EQ(xml, "<r />");
	expect_elements(xml, {{"/r", ""}});
}

TEST(WriterFormatting, InvalidWidthsFailBeforeWritingAndKeepTheFirstError)
{
	for (size_t width : {Writer::max_indent_width + 1, std::numeric_limits<size_t>::max()}) {
		int calls = 0;
		Writer writer([&](char const *, int n) { ++calls; return n; });
		EXPECT_FALSE(writer.set_indent_width(width));
		EXPECT_EQ(writer.indent_width(), 4u);
		EXPECT_EQ(writer.error().code, Code::InvalidOperation);
		EXPECT_EQ(writer.error().offset, 0u);
		auto first = writer.error();
		EXPECT_FALSE(writer.set_indent_width(0));
		EXPECT_FALSE(writer.start_document());
		EXPECT_FALSE(writer.start_element("r"));
		EXPECT_FALSE(writer.end_document());
		writer.output_error("later error");
		EXPECT_EQ(writer.error().code, first.code);
		EXPECT_EQ(writer.error().offset, first.offset);
		EXPECT_EQ(writer.error().message, first.message);
		EXPECT_EQ(calls, 0);
	}
}

TEST(WriterFormatting, SettingsCannotChangeAfterOutputOperations)
{
	for (int operation = 0; operation < 6; ++operation) {
		SCOPED_TRACE(operation);
		std::string xml;
		Writer writer([&](char const *p, int n) { xml.append(p, size_t(n)); return n; });
		ASSERT_TRUE(writer.set_indent_width(2));
		switch (operation) {
		case 0: ASSERT_TRUE(writer.start_document()); break;
		case 1: ASSERT_TRUE(writer.start_element("r")); break;
		case 2: ASSERT_TRUE(writer.write_characters("")); break;
		case 3: ASSERT_TRUE(writer.end_element()); break;
		case 4: ASSERT_TRUE(writer.end_document()); break;
		case 5:
			ASSERT_TRUE(writer.start_element("r"));
			ASSERT_TRUE(writer.end_document());
			break;
		}
		std::string before = xml;
		EXPECT_FALSE(writer.set_indent_width(0));
		EXPECT_EQ(writer.indent_width(), 2u);
		EXPECT_EQ(writer.error().code, Code::InvalidOperation);
		EXPECT_EQ(writer.error().offset, before.size());
		EXPECT_FALSE(writer.end_document());
		EXPECT_EQ(xml, before);
	}
}

TEST(WriterFormatting, SettingsCannotChangeFromOutputCallback)
{
	Writer *active = nullptr;
	int calls = 0;
	Writer writer([&](char const *, int n) {
		++calls;
		EXPECT_FALSE(active->set_indent_width(0));
		return n;
	});
	active = &writer;
	ASSERT_TRUE(writer.start_element("r"));
	EXPECT_FALSE(writer.end_document());
	EXPECT_EQ(writer.error().code, Code::InvalidOperation);
	EXPECT_EQ(writer.indent_width(), 4u);
	EXPECT_FALSE(writer.end_document());
	EXPECT_EQ(calls, 1);
}

TEST(WriterFormatting, TextAndPublicEncoderKeepTheirWhitespaceRules)
{
	for (bool utf8through : {false, true}) {
		std::string encoded = "keep";
		xstream::ErrorInfo error;
		ASSERT_TRUE(xstream::html_encode("\t\n\r", &encoded, &error, utf8through));
		EXPECT_EQ(encoded, "\t\n&#13;");
	}
	std::string xml;
	Writer writer([&](char const *p, int n) { xml.append(p, size_t(n)); return n; });
	ASSERT_TRUE(writer.start_element("r"));
	ASSERT_TRUE(writer.write_characters("\t\n\r"));
	ASSERT_TRUE(writer.end_document());
	EXPECT_EQ(xml, "<r>\t\n&#13;</r>\n");
	expect_elements(xml, {{"/r", "\t\n\r"}});
}

TEST(WriterFormatting, AttributeConversionErrorsUseOriginalValueOffsets)
{
	for (std::string const value : {"\t\n\x80", "\t\r\x01"}) {
		int calls = 0;
		Writer writer([&](char const *, int n) { ++calls; return n; });
		ASSERT_TRUE(writer.start_element("r"));
		EXPECT_FALSE(writer.write_attribute("a", value));
		EXPECT_EQ(writer.error().code, value[2] == '\x01' ? Code::InvalidCharacter : Code::InvalidUtf8);
		EXPECT_EQ(writer.error().offset, 2u);
		auto first = writer.error();
		EXPECT_FALSE(writer.write_attribute("b", "valid"));
		EXPECT_FALSE(writer.write_characters("valid"));
		EXPECT_FALSE(writer.end_document());
		EXPECT_FALSE(writer.set_indent_width(0));
		EXPECT_EQ(writer.error().code, first.code);
		EXPECT_EQ(writer.error().offset, first.offset);
		EXPECT_EQ(writer.error().message, first.message);
		EXPECT_EQ(calls, 0);
	}
}

TEST(WriterFormatting, CompactOutputFailureKeepsItsFirstDiagnostic)
{
	std::string xml;
	int calls = 0;
	Writer writer([&](char const *p, int) {
		if (++calls == 1) { xml.append(p, 2); return 2; }
		return 0;
	});
	ASSERT_TRUE(writer.set_indent_width(0));
	ASSERT_TRUE(writer.start_element("r"));
	EXPECT_FALSE(writer.end_document());
	EXPECT_EQ(xml, "<r");
	EXPECT_EQ(writer.error().code, Code::OutputError);
	EXPECT_EQ(writer.error().offset, 2u);
	auto first = writer.error();
	EXPECT_FALSE(writer.set_indent_width(4));
	EXPECT_FALSE(writer.end_document());
	EXPECT_EQ(writer.error().code, first.code);
	EXPECT_EQ(writer.error().offset, first.offset);
	EXPECT_EQ(writer.error().message, first.message);
	EXPECT_EQ(calls, 2);
}
