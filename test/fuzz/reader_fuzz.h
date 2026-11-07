#ifndef XSTREAM_READER_FUZZ_H
#define XSTREAM_READER_FUZZ_H

#include <xstream.h>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <tuple>

namespace reader_fuzz {

using Reader = xstream::Reader;
using Result = Reader::ReadResult;
using Code = Reader::ErrorCode;
using Event = std::tuple<Reader::StateType, std::string, std::string,
	std::vector<std::pair<std::string, std::string>>, std::string, std::string>;
constexpr size_t header_size = 16;
constexpr size_t max_xml_size = 4096;

inline void require(bool condition, char const *message)
{
	if (!condition) throw std::logic_error(message); // Active under -DNDEBUG too.
}

struct Config {
	std::array<unsigned char, header_size> control{};
	Reader::Limits limits;
	bool permissive() const { return (control[0] & 1) != 0; }
	size_t pick(size_t index, std::array<size_t, 5> values) const
	{
		return values[(control[index] ^ '0') % values.size()];
	}
	explicit Config(std::string_view input)
	{
		control.fill('4');
		if (input.size() >= header_size) std::copy_n(input.begin(), header_size, control.begin());
		limits.max_depth = pick(1, {0, 1, 2, 4, 16});
		limits.max_name_bytes = pick(2, {0, 1, 4, 16, 64});
		limits.max_attributes = pick(3, {0, 1, 2, 8, 16});
		limits.max_attribute_value_bytes = pick(4, {0, 1, 4, 32, 256});
		limits.max_token_bytes = pick(5, {0, 1, 4, 32, 1024});
		limits.max_element_text_bytes = pick(6, {0, 1, 4, 32, 256});
		limits.max_total_text_bytes = pick(7, {0, 1, 4, 64, 512});
		limits.max_text_parts = pick(8, {0, 1, 2, 8, 64});
		limits.max_total_attribute_bytes = pick(9, {0, 1, 8, 64, 512});
		limits.max_path_bytes = pick(10, {0, 1, 4, 32, 128});
		// Borrowed input has no streaming buffer, and rejects an oversized document
		// before parsing any events. Keep ingestion limits out of the differential
		// comparison, then exercise them separately below.
		limits.max_document_bytes = max_xml_size;
		limits.max_buffer_bytes = max_xml_size;
	}
	std::vector<size_t> chunks(size_t size) const
	{
		uint32_t random = 0x9e3779b9;
		for (size_t i = 11; i < control.size(); ++i) random = random * 33 + control[i];
		if (random == 0) random = 1;
		std::vector<size_t> out;
		while (size) {
			random ^= random << 13;
			random ^= random >> 17;
			random ^= random << 5;
			size_t mode = control[11] % 4;
			size_t length = mode == 0 ? size : (mode == 1 ? 1 : 1 + random % (mode == 2 ? 7 : 64));
			length = std::min(size, length);
			out.push_back(length);
			size -= length;
		}
		return out;
	}
};

struct Outcome {
	std::vector<Event> events;
	std::vector<std::string> open;
	Result result = Result::NeedInput;
	Reader::ErrorInfo error;
	size_t advances = 0;
};

inline void check_budgets(Reader const &reader, size_t size)
{
	auto stats = reader.statistics();
	auto const &limits = reader.limits();
	require(stats.buffer_capacity <= limits.max_buffer_bytes, "buffer capacity exceeds limit");
	require(stats.buffered_bytes <= limits.max_buffer_bytes, "buffered input exceeds limit");
	require(stats.retained_text_bytes <= limits.max_total_text_bytes, "text retention exceeds limit");
	require(stats.retained_text_parts <= limits.max_text_parts, "text fragments exceed limit");
	require(stats.retained_attribute_bytes <= limits.max_total_attribute_bytes, "attribute retention exceeds limit");
	require(stats.path_bytes <= limits.max_path_bytes, "path exceeds limit");
	require(stats.scan_steps <= size * 16 + 64, "excessive scanner work");
	require(stats.copied_input_bytes <= size * 4 + 512, "excessive input copying");
}

inline void pump(Reader *reader, Outcome *out, size_t size)
{
	for (;;) {
		require(++out->advances <= size * 3 + 16, "reader did not stop returning events or waiting");
		out->result = reader->advance();
		check_budgets(*reader, size);
		if (out->result != Result::Event) {
			out->error = reader->error();
			return;
		}
		require(reader->state() != Reader::None && reader->state() != Reader::Error, "invalid event state");
		auto const &limits = reader->limits();
		std::vector<std::pair<std::string, std::string>> attributes;
		for (auto const &a : reader->attributes()) {
			auto value = a.second.to_string();
			require(a.first.size() <= limits.max_name_bytes, "attribute name exceeds limit");
			require(value.size() <= limits.max_attribute_value_bytes, "attribute value exceeds limit");
			attributes.emplace_back(a.first, std::move(value));
		}
		require(attributes.size() <= limits.max_attributes, "attribute count exceeds limit");
		require(reader->name().size() <= limits.max_name_bytes, "element name exceeds limit");
		std::string text = reader->text(), part;
		require(text.size() <= limits.max_element_text_bytes, "element text exceeds limit");
		if (reader->is_characters() || reader->state() == Reader::Comment) {
			auto decoded = reader->characters().decode();
			part.assign(decoded.begin(), decoded.end());
			require(part.size() <= limits.max_token_bytes, "character event exceeds token limit");
		}
		if (reader->path().empty()) {
			auto stats = reader->statistics();
			require(text.empty() && stats.retained_text_bytes == 0 && stats.retained_text_parts == 0,
				"text outside the root was retained as element text");
		}
		if (reader->is_start_element()) {
			out->open.push_back(reader->path());
			require(out->open.size() <= limits.max_depth, "element depth exceeds limit");
		} else if (reader->is_end_element()) {
			require(!out->open.empty() && out->open.back() == reader->path(), "end event has no matching start");
			out->open.pop_back();
		}
		out->events.emplace_back(reader->state(), reader->name(), reader->path(), attributes, text, part);
	}
}

inline void check_terminal(Reader *reader, Outcome const &out, size_t size)
{
	require(out.result == Result::Error || out.result == Result::EndDocument, "EOF did not terminate parsing");
	if (out.result == Result::EndDocument) {
		require(out.open.empty() && out.error.code == Code::None, "incorrect successful completion");
	} else {
		require(out.error.code != Code::None && !out.error.message.empty(), "missing error diagnostic");
		require(out.error.offset <= size, "error offset is outside the input");
		reader->input("ignored");
		reader->finish_input();
		reader->input_error("later I/O error");
	}
	for (int i = 0; i < 2; ++i) {
		require(reader->advance() == out.result, "terminal result is not sticky");
		require(reader->error().code == out.error.code && reader->error().offset == out.error.offset
			&& reader->error().message == out.error.message, "first diagnostic was overwritten");
	}
	check_budgets(*reader, size);
}

inline Outcome borrowed(std::string_view xml, Config const &config)
{
	Reader reader(xml, config.limits);
	reader.set_allow_invalid_comments(config.permissive());
	Outcome out;
	pump(&reader, &out, xml.size());
	check_terminal(&reader, out, xml.size());
	return out;
}

inline Outcome pushed(std::string_view xml, Config const &config, std::vector<size_t> const &chunks)
{
	Reader reader(config.limits);
	reader.set_allow_invalid_comments(config.permissive());
	Outcome out;
	size_t position = 0;
	for (size_t length : chunks) {
		reader.input(xml.substr(position, length));
		position += length;
		pump(&reader, &out, xml.size());
		if (out.result == Result::Error) break;
		require(out.result == Result::NeedInput, "stream ended without EOF");
	}
	reader.finish_input();
	pump(&reader, &out, xml.size());
	check_terminal(&reader, out, xml.size());
	return out;
}

inline Outcome callback(std::string_view xml, Config const &config, std::vector<size_t> const &chunks)
{
	Reader reader(config.limits);
	reader.set_allow_invalid_comments(config.permissive());
	size_t position = 0, chunk = 0, calls = 0;
	bool stall = true;
	reader.parse([&] {
		require(++calls <= xml.size() * 2 + 4, "excessive input requests");
		stall = !stall;
		if (!stall) return; // A no-progress callback must yield NeedInput, not EOF.
		if (chunk == chunks.size()) reader.finish_input();
		else {
			size_t length = chunks[chunk++];
			reader.input(xml.substr(position, length));
			position += length;
		}
	});
	Outcome out;
	do { pump(&reader, &out, xml.size()); } while (out.result == Result::NeedInput);
	check_terminal(&reader, out, xml.size());
	return out;
}

inline void compare(Outcome const &a, Outcome const &b)
{
	require(a.events == b.events, "one-shot and streaming events differ");
	require(a.result == b.result, "one-shot and streaming final results differ");
	require(a.error.code == b.error.code, "one-shot and streaming error codes differ");
	require(a.error.offset == b.error.offset, "one-shot and streaming error offsets differ");
	require(a.error.message == b.error.message, "one-shot and streaming diagnostics differ");
}

inline void check_input(std::string_view input)
{
	if (input.size() > header_size + max_xml_size) return;
	Config config(input);
	std::string_view xml = input.size() >= header_size ? input.substr(header_size) : input;
	auto chunks = config.chunks(xml.size());
	auto expected = borrowed(xml, config);
	compare(expected, pushed(xml, config, chunks));
	compare(expected, callback(xml, config, chunks));
	if (config.control[15] & 1) compare(expected, pushed(xml, config, std::vector<size_t>(xml.size(), 1)));

	Config ingestion = config;
	ingestion.limits.max_document_bytes = config.pick(12, {0, 1, 4, 32, max_xml_size});
	ingestion.limits.max_buffer_bytes = config.pick(13, {0, 1, 4, 32, max_xml_size});
	Reader reader(ingestion.limits);
	reader.set_allow_invalid_comments(ingestion.permissive());
	reader.input(xml);
	if (xml.size() > ingestion.limits.max_document_bytes || xml.size() > ingestion.limits.max_buffer_bytes) {
		require(reader.error().code == Code::LimitExceeded, "oversized input was not rejected before copying");
		require(reader.statistics().buffer_capacity == 0, "oversized initial input allocated a buffer");
	}
	reader.finish_input();
	Outcome out;
	pump(&reader, &out, xml.size());
	check_terminal(&reader, out, xml.size());
	(void)pushed(xml, ingestion, chunks); // Small buffers may intentionally differ from borrowed input.
}

} // namespace reader_fuzz
#endif
