#include <xstream.h>

// A standalone integration check: no GoogleTest and no exception syntax.
int main()
{
	std::string output = "unchanged";
	xstream::ErrorInfo error;
	if (xstream::html_decode("&unknown;", &output, &error)
		|| error.code != xstream::ErrorCode::InvalidCharacterReference || output != "unchanged") return 1;
	if (!xstream::html_decode("&#x1F600;&amp;", &output, &error) || error.code != xstream::ErrorCode::None) return 2;
	if (xstream::html_encode("\x80", &output, &error) || error.code != xstream::ErrorCode::InvalidUtf8) return 3;
	xstream::Reader reader("<r>&unknown;</r>");
	while (reader.next()) {}
	if (reader.result() != xstream::Reader::ReadResult::Error
		|| reader.error().code != xstream::ErrorCode::InvalidCharacterReference) return 4;
	std::string xml;
	xstream::Writer writer([&](char const *p, int n) { xml.append(p, size_t(n)); return n; });
	if (!writer.start_element("r") || !writer.write_characters(output) || !writer.end_document() || !writer.good()) return 5;
	xstream::Writer failed([](char const *, int) { return -1; });
	if (!failed.start_element("r") || failed.end_document()
		|| failed.error().code != xstream::ErrorCode::OutputError) return 6;
	xstream::Writer invalid([](char const *, int n) { return n; });
	if (!invalid.start_element("r") || invalid.write_characters("\x01")
		|| invalid.error().code != xstream::ErrorCode::InvalidCharacter || invalid.end_document()) return 7;
	xstream::Reader::Limits limits;
	limits.max_element_text_bytes = 0;
	limits.max_total_text_bytes = 0;
	limits.max_text_parts = 0;
	xstream::Reader outside(" \n<!---->\t<r/>\r", limits);
	std::vector<xstream::Reader::StateType> expected = {
		xstream::Reader::Characters, xstream::Reader::Comment, xstream::Reader::Characters,
		xstream::Reader::StartElement, xstream::Reader::EndElement, xstream::Reader::Characters
	};
	size_t events = 0;
	std::string whitespace;
	while (outside.next()) {
		if (events >= expected.size() || outside.state() != expected[events++]) return 8;
		if (!outside.text().empty() || outside.statistics().retained_text_bytes != 0
			|| outside.statistics().retained_text_parts != 0) return 9;
		if (outside.is_characters()) {
			auto bytes = outside.characters().decode();
			whitespace.append(bytes.begin(), bytes.end());
		}
	}
	if (events != expected.size() || outside.result() != xstream::Reader::ReadResult::EndDocument
		|| outside.error().code != xstream::ErrorCode::None || whitespace != " \n\t\r") return 10;
	std::string compact;
	xstream::Writer compact_writer([&](char const *p, int n) { compact.append(p, size_t(n)); return n; });
	if (!compact_writer.set_indent_width(0) || compact_writer.indent_width() != 0
		|| !compact_writer.start_element("r") || !compact_writer.write_attribute("a", "\t\n\r")
		|| !compact_writer.write_characters("x") || !compact_writer.start_element("c")
		|| !compact_writer.end_element() || !compact_writer.write_characters("y")
		|| !compact_writer.end_document() || !compact_writer.good()
		|| compact != "<r a=\"&#9;&#10;&#13;\">x<c />y</r>") return 11;
	xstream::Writer invalid_width([](char const *, int n) { return n; });
	if (invalid_width.set_indent_width(xstream::Writer::max_indent_width + 1)
		|| invalid_width.error().code != xstream::ErrorCode::InvalidOperation
		|| invalid_width.end_document()) return 12;
	return 0;
}
