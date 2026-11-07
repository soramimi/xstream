// Xstream - Header-only Streaming pull-based XML/HTML Parser and Generator
// Copyright (C) 2026 S.Fuchita (soramimi)
// This software is distributed under the MIT license.

#ifndef XSTREAM_H
#define XSTREAM_H

#include <algorithm>
#include <assert.h>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace xstream {

enum class ErrorCode {
	None,
	InvalidSyntax,
	UnexpectedEof,
	MismatchedEndTag,
	InvalidOperation,
	InputError,
	LimitExceeded,
	UnsupportedDeclaration,
	InvalidCharacterReference,
	InvalidUtf8,
	InvalidCharacter,
	UnsupportedEncoding,
	OutputError
};

struct ErrorInfo {
	ErrorCode code = ErrorCode::None;
	size_t offset = 0;
	std::string message;
};

namespace detail {

	class FlagGuard {
		bool *const flag_;

	public:
		explicit FlagGuard(bool *flag)
			: flag_(flag)
		{
			*flag_ = true;
		}
		~FlagGuard() { *flag_ = false; }
		FlagGuard(FlagGuard const &) = delete;
	};

	inline bool conversion_error(ErrorInfo *error, ErrorCode code, size_t offset, char const *message)
	{
		if (error) *error = { code, offset, message };
		return false;
	}

	inline bool xml_char(unsigned int c)
	{
		return c == 9 || c == 10 || c == 13 || (c >= 0x20 && c <= 0xd7ff)
			|| (c >= 0xe000 && c <= 0xfffd) || (c >= 0x10000 && c <= 0x10ffff);
	}

	inline bool name_start(unsigned int c)
	{
		return c == ':' || c == '_' || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
			|| (c >= 0xc0 && c <= 0xd6) || (c >= 0xd8 && c <= 0xf6)
			|| (c >= 0xf8 && c <= 0x2ff) || (c >= 0x370 && c <= 0x37d)
			|| (c >= 0x37f && c <= 0x1fff) || (c >= 0x200c && c <= 0x200d)
			|| (c >= 0x2070 && c <= 0x218f) || (c >= 0x2c00 && c <= 0x2fef)
			|| (c >= 0x3001 && c <= 0xd7ff) || (c >= 0xf900 && c <= 0xfdcf)
			|| (c >= 0xfdf0 && c <= 0xfffd) || (c >= 0x10000 && c <= 0xeffff);
	}

	inline bool name_char(unsigned int c)
	{
		return name_start(c) || c == '-' || c == '.' || (c >= '0' && c <= '9')
			|| c == 0xb7 || (c >= 0x300 && c <= 0x36f) || (c >= 0x203f && c <= 0x2040);
	}

	enum class Utf8Result {
		Complete,
		Incomplete,
		Invalid
	};

	template <class ByteAt>
	Utf8Result read_utf8(size_t available, ByteAt byte_at, unsigned int *c, size_t *width)
	{
		if (!c || !width) return Utf8Result::Invalid;
		if (available == 0) return Utf8Result::Incomplete;
		unsigned int first = byte_at(0);
		*width = 1;
		if (first < 0x80) {
			*c = first;
			return Utf8Result::Complete;
		}
		if (first >= 0xc2 && first <= 0xdf) {
			*width = 2;
			*c = first & 0x1f;
		} else if (first >= 0xe0 && first <= 0xef) {
			*width = 3;
			*c = first & 0x0f;
		} else if (first >= 0xf0 && first <= 0xf4) {
			*width = 4;
			*c = first & 0x07;
		} else
			return Utf8Result::Invalid;
		for (size_t i = 1; i < *width; ++i) {
			if (i >= available) return Utf8Result::Incomplete;
			unsigned int b = byte_at(i);
			if (b < 0x80 || b > 0xbf) return Utf8Result::Invalid;
			if (i == 1 && ((first == 0xe0 && b < 0xa0) || (first == 0xed && b > 0x9f) || (first == 0xf0 && b < 0x90) || (first == 0xf4 && b > 0x8f))) return Utf8Result::Invalid;
			*c = (*c << 6) | (b & 0x3f);
		}
		return Utf8Result::Complete;
	}

	inline void append_utf8(std::string *out, unsigned int c)
	{
		if (c < 0x80)
			out->push_back(char(c));
		else if (c < 0x800) {
			out->push_back(char(0xc0 | (c >> 6)));
			out->push_back(char(0x80 | (c & 0x3f)));
		} else if (c < 0x10000) {
			out->push_back(char(0xe0 | (c >> 12)));
			out->push_back(char(0x80 | ((c >> 6) & 0x3f)));
			out->push_back(char(0x80 | (c & 0x3f)));
		} else {
			out->push_back(char(0xf0 | (c >> 18)));
			out->push_back(char(0x80 | ((c >> 12) & 0x3f)));
			out->push_back(char(0x80 | ((c >> 6) & 0x3f)));
			out->push_back(char(0x80 | (c & 0x3f)));
		}
	}

	struct Reference {
		enum class Kind {
			Start,
			Hash,
			Decimal,
			Hex,
			Named
		};
		enum class Result {
			More,
			Complete,
			Invalid
		};
		Kind kind = Kind::Start;
		bool active = false, bad = false, digit_seen = false;
		unsigned int value = 0;
		char name[5] = { };
		size_t name_size = 0;
		void start()
		{
			*this = { };
			active = true;
		}
		void add_digit(unsigned int c, unsigned int base)
		{
			unsigned int digit = c >= '0' && c <= '9' ? c - '0'
													  : (c >= 'a' && c <= 'f' ? c - 'a' + 10 : (c >= 'A' && c <= 'F' ? c - 'A' + 10 : 16));
			if (digit >= base || value > (0x10ffff - digit) / base)
				bad = true;
			else if (!bad)
				value = value * base + digit;
			digit_seen = true;
		}
		Result push(unsigned int c)
		{
			if (c == ';') {
				active = false;
				if (kind == Kind::Named) {
					std::string_view n(name, name_size);
					if (n == "amp")
						value = '&';
					else if (n == "lt")
						value = '<';
					else if (n == "gt")
						value = '>';
					else if (n == "quot")
						value = '"';
					else if (n == "apos")
						value = '\'';
					else
						bad = true;
				} else if ((kind != Kind::Decimal && kind != Kind::Hex) || !digit_seen)
					bad = true;
				return !bad && xml_char(value) ? Result::Complete : Result::Invalid;
			}
			if (kind == Kind::Start) {
				if (c == '#') {
					kind = Kind::Hash;
					return Result::More;
				}
				kind = Kind::Named;
			} else if (kind == Kind::Hash) {
				if (c == 'x') {
					kind = Kind::Hex;
					return Result::More;
				}
				kind = Kind::Decimal;
			}
			if (kind == Kind::Named) {
				if (c > 0x7f || name_size == sizeof(name))
					bad = true;
				else
					name[name_size++] = char(c);
			} else
				add_digit(c, kind == Kind::Hex ? 16 : 10);
			return Result::More;
		}
	};

	inline bool decode_references(std::string_view input, std::string *output, ErrorInfo *error)
	{
		if (!output) return detail::conversion_error(error, ErrorCode::InvalidOperation, 0, "Output pointer must not be null");
		std::string out;
		if (input.size() > out.max_size()) return conversion_error(error, ErrorCode::LimitExceeded, 0, "Decoded text is too large");
		out.reserve(input.size());
		Reference ref;
		size_t reference_begin = 0;
		for (size_t i = 0; i < input.size();) {
			unsigned int c = 0;
			size_t width = 0;
			if (read_utf8(input.size() - i, [&](size_t j) { return (unsigned char)input[i + j]; }, &c, &width) != Utf8Result::Complete) return conversion_error(error, ErrorCode::InvalidUtf8, i, "Invalid or truncated UTF-8 sequence");
			if (!xml_char(c)) return conversion_error(error, ErrorCode::InvalidCharacter, i, "Character is not allowed in XML 1.0");
			if (ref.active) {
				auto result = ref.push(c);
				if (result == Reference::Result::Invalid) return conversion_error(error, ErrorCode::InvalidCharacterReference,
					reference_begin, "Invalid XML character reference");
				if (result == Reference::Result::Complete) append_utf8(&out, ref.value);
			} else if (c == '&') {
				reference_begin = i;
				ref.start();
			} else
				out.append(input.data() + i, width);
			i += width;
		}
		if (ref.active) return conversion_error(error, ErrorCode::InvalidCharacterReference, reference_begin, "Incomplete XML character reference");
		output->swap(out);
		if (error) *error = { };
		return true;
	}

	inline bool encode_characters(std::string_view input, std::string *output, ErrorInfo *error, bool utf8through,
		bool attribute = false)
	{
		if (!output) return detail::conversion_error(error, ErrorCode::InvalidOperation, 0, "Output pointer must not be null");
		std::string out;
		if (input.size() > out.max_size()) return conversion_error(error, ErrorCode::LimitExceeded, 0, "Encoded text is too large");
		out.reserve(input.size());
		for (size_t i = 0; i < input.size();) {
			unsigned int c = 0;
			size_t width = 0;
			if (read_utf8(input.size() - i, [&](size_t j) { return (unsigned char)input[i + j]; }, &c, &width) != Utf8Result::Complete) return conversion_error(error, ErrorCode::InvalidUtf8, i, "Invalid or truncated UTF-8 sequence");
			if (!xml_char(c)) return conversion_error(error, ErrorCode::InvalidCharacter, i, "Character is not allowed in XML 1.0");
			std::string numeric;
			std::string_view replacement;
			switch (c) {
			case '&':
				replacement = "&amp;";
				break;
			case '<':
				replacement = "&lt;";
				break;
			case '>':
				replacement = "&gt;";
				break;
			case '"':
				replacement = "&quot;";
				break;
			case '\'':
				replacement = "&apos;";
				break;
			default:
				if (c == '\r' || (attribute && (c == '\t' || c == '\n')) || (!utf8through && c >= 0x80)) {
					numeric = "&#" + std::to_string(c) + ";";
					replacement = numeric;
				} else
					replacement = input.substr(i, width);
			}
			if (replacement.size() > out.max_size() - out.size()) return conversion_error(error, ErrorCode::LimitExceeded, i, "Encoded text is too large");
			out.append(replacement.data(), replacement.size());
			i += width;
		}
		output->swap(out);
		if (error) *error = { };
		return true;
	}

} // namespace detail

[[nodiscard]] inline bool html_encode(std::string_view str, std::string *output, ErrorInfo *error = nullptr, bool utf8through = true)
{
	return detail::encode_characters(str, output, error, utf8through);
}

inline std::string html_encode(std::string_view str, bool utf8through = true, ErrorInfo *error = nullptr)
{
	std::string out;
	(void)html_encode(str, &out, error, utf8through);
	return out;
}

inline std::string html_encode(std::string_view str, ErrorInfo *error, bool utf8through = true)
{
	return html_encode(str, utf8through, error);
}

[[nodiscard]] inline bool html_decode(std::string_view str, std::string *output, ErrorInfo *error = nullptr)
{
	return detail::decode_references(str, output, error);
}

inline std::string html_decode(std::string_view str, ErrorInfo *error = nullptr)
{
	std::string out;
	(void)html_decode(str, &out, error);
	return out;
}

class Reader {
private:
	static inline void vecprint(std::vector<char> *out, char c)
	{
		out->push_back(c);
	}

	static inline void vecprint(std::vector<char> *out, char const *s)
	{
		out->insert(out->end(), s, s + strlen(s));
	}

	static inline std::string_view to_string(std::vector<char> const &vec)
	{
		if (!vec.empty()) {
			return { vec.data(), vec.size() };
		}
		return { };
	}

public:
	enum StateType {
		None,
		Error,
		Comment,
		StartElement,
		EndElement,
		Characters,
		Declaration
	};
	enum class ReadResult {
		Event,
		NeedInput,
		EndDocument,
		SubtreeEnd,
		Error
	};
	using ErrorCode = xstream::ErrorCode;
	using ErrorInfo = xstream::ErrorInfo;
	struct Limits {
		size_t max_depth = 256;
		size_t max_name_bytes = 1024;
		size_t max_attributes = 256;
		size_t max_attribute_value_bytes = 1024 * 1024;
		size_t max_token_bytes = 4 * 1024 * 1024;
		size_t max_buffer_bytes = 8 * 1024 * 1024;
		size_t max_element_text_bytes = 8 * 1024 * 1024;
		size_t max_total_text_bytes = 16 * 1024 * 1024;
		size_t max_text_parts = 65536;
		size_t max_total_attribute_bytes = 8 * 1024 * 1024;
		size_t max_path_bytes = 64 * 1024;
		size_t max_document_bytes = 64 * 1024 * 1024;
	};
	struct Statistics {
		size_t scan_steps = 0;
		size_t copied_input_bytes = 0;
		size_t appended_path_bytes = 0;
		size_t buffer_capacity = 0;
		size_t buffered_bytes = 0;
		size_t retained_text_bytes = 0;
		size_t retained_text_parts = 0;
		size_t retained_attribute_bytes = 0;
		size_t path_bytes = 0;
	};

private:
	struct CharPart {
		enum Type {
			Text,
			CDATA,
			Comment,
		};
		Type type_ = Text;
		std::string sv_;
		CharPart() = default;
		CharPart(Type type, char const *begin, char const *end)
			: type_(type)
			, sv_(begin, end)
		{
		}
		Type type() const
		{
			return type_;
		}
		bool decode(std::vector<char> *output, ErrorInfo *error = nullptr) const
		{
			if (!output) return detail::conversion_error(error, ErrorCode::InvalidOperation, 0, "Output pointer must not be null");
			std::string value;
			if (type_ == Text) {
				if (!html_decode(sv_, &value, error)) return false;
			} else if (type_ == CDATA)
				value = sv_;
			if (value.size() > output->max_size()) return detail::conversion_error(error, ErrorCode::LimitExceeded, 0, "Decoded text is too large");
			std::vector<char> result(value.begin(), value.end());
			output->swap(result);
			if (error) *error = { };
			return true;
		}
		std::vector<char> decode() const
		{
			std::vector<char> out;
			(void)decode(&out);
			return out;
		}
	};
	class EncodedCharacters {
		friend class Reader;

	private:
		std::vector<CharPart> chars_;
		void clear()
		{
			chars_.clear();
		}
		void append(CharPart::Type type, char const *begin, char const *end)
		{
			if (begin != end) {
				chars_.emplace_back(type, begin, end);
			}
		}

	public:
		EncodedCharacters() = default;
		void append(EncodedCharacters *a)
		{
			EncodedCharacters b = std::move(*a);
			for (size_t i = 0; i < b.chars_.size(); ++i) {
				chars_.push_back(std::move(b.chars_[i]));
			}
		}
		bool to_string(std::string *output, ErrorInfo *error = nullptr) const
		{
			if (!output) return detail::conversion_error(error, ErrorCode::InvalidOperation, 0, "Output pointer must not be null");
			std::string out;
			for (auto const &part : chars_) {
				std::vector<char> value;
				if (!part.decode(&value, error)) return false;
				if (value.size() > out.max_size() - out.size()) return detail::conversion_error(error, ErrorCode::LimitExceeded, out.size(), "Decoded text is too large");
				out.append(value.begin(), value.end());
			}
			output->swap(out);
			if (error) *error = { };
			return true;
		}
		std::string to_string() const
		{
			std::string out;
			(void)to_string(&out);
			return out;
		}
	};
	class EscapedAttributeValue {
	private:
		std::string sv_;

	public:
		EscapedAttributeValue() = default;
		EscapedAttributeValue(std::string sv)
			: sv_(sv)
		{
		}
		std::string to_string() const
		{
			return html_decode(sv_);
		}
		bool to_string(std::string *output, ErrorInfo *error = nullptr) const
		{
			if (!output) return detail::conversion_error(error, ErrorCode::InvalidOperation, 0, "Output pointer must not be null");
			return html_decode(sv_, output, error);
		}
		operator std::string() const
		{
			return to_string();
		}
	};
	struct Tag {
		std::string name;
		size_t parent_path_size = 0;
		size_t text_bytes = 0;
		size_t attribute_bytes = 0;
		std::vector<std::pair<std::string, std::string>> atts;
		EncodedCharacters chars;
		Tag() = default;
	};
	enum class Phase {
		Start,
		Text,
		AfterLt,
		Bang,
		CommentDash,
		CdataPrefix,
		Comment,
		Cdata,
		NameFirst,
		Name,
		EndSpace,
		TagGap,
		AttributeName,
		AttributeEquals,
		AttributeQuote,
		AttributeValue,
		TagBracket
	};
	struct AttributeRange {
		size_t key_begin, key_end, value_begin, value_end;
	};
	struct Scan {
		Phase phase = Phase::Start;
		size_t cursor = 0;
		size_t name_begin = 0, name_end = 0;
		size_t key_begin = 0, key_end = 0, value_begin = 0;
		size_t content_begin = 0, matched = 0;
		size_t attribute_bytes = 0;
		size_t character_bytes = 1, reference_begin = 0;
		detail::Reference reference;
		char marker = 0, quote = 0;
		bool separated = false, empty_element = false;
		std::vector<AttributeRange> attributes;
	};
	Limits limits_;
	bool allow_invalid_comments_ = false;
	struct D {
		bool not_enough_input = false;
		bool input_finished = false;
		bool root_seen = false;
		bool advancing = false;
		ReadResult result = ReadResult::NeedInput;
		ErrorInfo error;
		std::optional<std::vector<char>> input_buffer;
		size_t buffer_begin = 0, buffer_size = 0;
		Scan scan;
		Statistics stats;
		std::optional<CharPart> outside_characters;
		std::function<void()> fn_input_calback;

		std::vector<int> depth_stack;
		bool hold = false;
		std::vector<Tag> tag_stack;
		std::string path;
		std::string_view source;
		size_t input_offset = 0;
		size_t ptr = 0;
		StateType state = None;
		bool next_end_element = false;
		std::string element_name;
	} d;

private:
	size_t input_size() const
	{
		return d.input_buffer ? d.buffer_size : d.source.size();
	}
	size_t ring_index(size_t position) const
	{
		size_t tail = d.input_buffer->size() - d.buffer_begin;
		return position >= tail ? position - tail : d.buffer_begin + position;
	}
	char input_byte(size_t position) const
	{
		return d.input_buffer ? (*d.input_buffer)[ring_index(position)] : d.source[position];
	}
	std::string copy_input(size_t begin, size_t size) const
	{
		if (size == 0) return { };
		if (!d.input_buffer) return std::string(d.source.substr(begin, size));
		size_t index = ring_index(begin);
		size_t first = std::min(size, d.input_buffer->size() - index);
		std::string value(d.input_buffer->data() + index, first);
		value.append(d.input_buffer->data(), size - first);
		return value;
	}
	static bool fits(size_t used, size_t extra, size_t maximum)
	{
		return used <= maximum && extra <= maximum - used;
	}
	static void count(size_t *counter, size_t extra = 1)
	{
		size_t maximum = std::numeric_limits<size_t>::max();
		*counter = extra > maximum - *counter ? maximum : *counter + extra;
	}
	static size_t grown_capacity(size_t current, size_t needed, size_t maximum)
	{
		size_t grown = current == 0 ? std::min(size_t(256), maximum)
									: (current > maximum - current ? maximum : current * 2);
		return std::max(needed, grown);
	}
	bool exceeded(char const *message, size_t offset)
	{
		fail(ErrorCode::LimitExceeded, offset, message);
		return false;
	}
	bool consume()
	{
		if (!fits(d.scan.cursor, d.scan.character_bytes, limits_.max_token_bytes)) {
			return exceeded("Maximum token size exceeded", d.input_offset + d.ptr + limits_.max_token_bytes);
		}
		d.scan.cursor += d.scan.character_bytes;
		return true;
	}
	bool check_name()
	{
		size_t size = d.scan.cursor - d.scan.name_begin;
		size_t offset = d.input_offset + d.ptr + d.scan.cursor;
		if (!fits(size, d.scan.character_bytes, std::min(limits_.max_name_bytes, std::string().max_size()))) {
			return exceeded("Maximum name size exceeded", offset);
		}
		if (d.scan.marker != '/') {
			size_t maximum = std::min(limits_.max_path_bytes, d.path.max_size());
			if (!fits(d.path.size(), 1, maximum) || !fits(size, d.scan.character_bytes, maximum)
				|| !fits(d.path.size() + 1, size + d.scan.character_bytes, maximum)) {
				return exceeded("Maximum path size exceeded", offset);
			}
		}
		return true;
	}
	bool check_attribute_bytes(size_t key_size, size_t value_size)
	{
		size_t maximum = limits_.max_total_attribute_bytes;
		size_t used = d.stats.retained_attribute_bytes;
		if (!fits(used, d.scan.attribute_bytes, maximum)
			|| !fits(used + d.scan.attribute_bytes, key_size, maximum)
			|| !fits(used + d.scan.attribute_bytes + key_size, value_size, maximum)) {
			return exceeded("Maximum retained attribute size exceeded", d.input_offset + d.ptr + d.scan.cursor);
		}
		return true;
	}
	bool check_text(size_t size, size_t offset)
	{
		if (d.tag_stack.size() == 1) {
			if (size > std::string().max_size()) return exceeded("Maximum text segment size exceeded", offset);
			return true; // Outside whitespace is an event, not retained element text.
		}
		if (!fits(d.tag_stack.back().text_bytes, size, std::min(limits_.max_element_text_bytes, std::string().max_size()))) {
			return exceeded("Maximum element text size exceeded", offset);
		}
		if (!fits(d.stats.retained_text_bytes, size, limits_.max_total_text_bytes)) {
			return exceeded("Maximum retained text size exceeded", offset);
		}
		if (d.stats.retained_text_parts >= limits_.max_text_parts
			|| d.tag_stack.back().chars.chars_.size() == d.tag_stack.back().chars.chars_.max_size()) {
			return exceeded("Maximum retained text part count exceeded", offset);
		}
		return true;
	}
	ReadResult fail(ErrorCode code, size_t offset, std::string message)
	{
		if (d.state != Error) {
			d.error = { code, offset, std::move(message) };
			d.state = Error;
			d.hold = false;
			d.next_end_element = false;
		}
		d.not_enough_input = false;
		return d.result = ReadResult::Error;
	}
	ReadResult incomplete()
	{
		if (d.input_finished) {
			return fail(ErrorCode::UnexpectedEof, d.input_offset + input_size(),
				"Unexpected end of XML input");
		}
		return ReadResult::NeedInput;
	}
	ReadResult bad_reference()
	{
		return fail(ErrorCode::InvalidCharacterReference, d.input_offset + d.ptr + d.scan.reference_begin,
			"Invalid or incomplete XML character reference");
	}
	bool check_reference(unsigned int c)
	{
		auto *scan = &d.scan;
		if (scan->reference.active) {
			if (scan->reference.push(c) == detail::Reference::Result::Invalid) {
				bad_reference();
				return false;
			}
		} else if (c == '&') {
			scan->reference_begin = scan->cursor;
			scan->reference.start();
		}
		return true;
	}
	static bool is_space(char c)
	{
		return c == ' ' || c == '\t' || c == '\r' || c == '\n';
	}
	void reset_stack()
	{
		d.tag_stack.clear();
		d.tag_stack.emplace_back();
		d.path.clear();
	}
	void pop_tag()
	{
		auto const &tag = d.tag_stack.back();
		d.path.resize(tag.parent_path_size);
		d.stats.retained_text_bytes -= tag.text_bytes;
		d.stats.retained_text_parts -= tag.chars.chars_.size();
		d.stats.retained_attribute_bytes -= tag.attribute_bytes;
		d.tag_stack.pop_back();
	}
	void init(std::string_view source, bool finished = true)
	{
		d.source = source;
		d.ptr = 0;
		d.input_finished = finished;
		reset_stack();
		if (source.size() > limits_.max_document_bytes) {
			exceeded("Maximum document size exceeded", limits_.max_document_bytes);
		}
	}
	std::string const &current_path() const
	{
		return d.path;
	}
	bool match_internal(char const *path) const
	{
		std::string_view candidate(path);
		return candidate == d.path || (candidate.size() == d.path.size() + 1 && candidate.back() == '/' && candidate.substr(0, d.path.size()) == d.path);
	}
	bool is_element_name(std::string_view name) const
	{
		return name == d.element_name;
	}
	bool is_element_name(char const *name) const
	{
		size_t n = d.element_name.size();
		return strncmp(name, d.element_name.data(), n) == 0 && name[n] == 0;
	}
	EncodedCharacters const &encoded_chars() const
	{
		assert(!d.tag_stack.empty());
		return d.tag_stack.back().chars;
	}

public:
	explicit Reader(Limits const &limits)
		: limits_(limits)
	{
		init({ }, false);
	}
	Reader(std::string_view source, Limits const &limits)
		: limits_(limits)
	{
		init(source);
	}
	Reader(char const *source, Limits const &limits)
		: Reader(std::string_view(source), limits)
	{
	}
	Reader(std::string const &&, Limits const &) = delete;
	Reader()
	{
		init({ }, false);
	}
	// These constructors borrow the source until parsing has finished.
	Reader(char const *source)
		: Reader(std::string_view(source))
	{
	}
	// Reject both const and non-const temporary strings before they become views.
	Reader(std::string const &&) = delete;
	Reader(char const *begin, char const *end)
	{
		init(begin == end ? std::string_view { } : std::string_view(begin, end - begin));
	}

	Reader(char const *ptr, size_t len)
	{
		init(len == 0 ? std::string_view { } : std::string_view(ptr, len));
	}

	Reader(std::string_view s)
	{
		init(s);
	}
	Limits const &limits() const
	{
		return limits_;
	}
	bool allow_invalid_comments() const
	{
		return allow_invalid_comments_;
	}
	// Configure before parsing any bytes; parse() preserves this setting.
	void set_allow_invalid_comments(bool allow)
	{
		if (d.state == Error) return;
		if (d.advancing || d.input_offset != 0 || d.ptr != 0
			|| d.scan.phase != Phase::Start || d.result == ReadResult::EndDocument) {
			fail(ErrorCode::InvalidOperation, d.input_offset + d.ptr,
				"Comment policy can only change before parsing");
			return;
		}
		allow_invalid_comments_ = allow;
	}
	void set_limits(Limits const &limits)
	{
		if (d.state == Error) return;
		if (d.advancing || d.input_finished || d.input_offset != 0 || input_size() != 0) {
			fail(ErrorCode::InvalidOperation, d.input_offset + d.ptr, "Limits can only change before input");
			return;
		}
		limits_ = limits;
	}
	Statistics statistics() const
	{
		Statistics stats = d.stats;
		stats.buffer_capacity = d.input_buffer ? d.input_buffer->size() : 0;
		stats.buffered_bytes = input_size() - d.ptr;
		stats.path_bytes = d.path.size();
		return stats;
	}
	void parse(std::function<void()> fn_input_calback)
	{
		if (d.advancing) {
			fail(ErrorCode::InvalidOperation, d.input_offset + d.ptr, "Cannot reset a reader while advancing");
			return;
		}
		d = { };
		init({ }, false);
		d.fn_input_calback = fn_input_calback;
	}
	void input(std::string_view in)
	{
		if (in.empty() || d.state == Error) return;
		if (d.input_finished) {
			fail(ErrorCode::InvalidOperation, d.input_offset + d.ptr, "Input supplied after EOF");
			return;
		}
		size_t received = d.input_offset + input_size();
		if (!fits(received, in.size(), limits_.max_document_bytes)) {
			exceeded("Maximum document size exceeded", limits_.max_document_bytes);
			return;
		}
		size_t remaining = input_size() - d.ptr;
		size_t maximum = std::min(limits_.max_buffer_bytes, std::vector<char>().max_size());
		if (!fits(remaining, in.size(), maximum)) {
			exceeded("Maximum input buffer size exceeded", d.input_offset + d.ptr + maximum);
			return;
		}
		size_t size = remaining + in.size();
		size_t capacity = d.input_buffer ? d.input_buffer->size() : 0;
		if (capacity < size) {
			std::vector<char> buffer(grown_capacity(capacity, size, maximum));
			if (remaining != 0) {
				if (d.input_buffer) {
					size_t index = ring_index(d.ptr);
					size_t first = std::min(remaining, capacity - index);
					std::copy_n(d.input_buffer->data() + index, first, buffer.data());
					std::copy_n(d.input_buffer->data(), remaining - first, buffer.data() + first);
				} else {
					std::copy_n(d.source.data() + d.ptr, remaining, buffer.data());
				}
			}
			std::copy(in.begin(), in.end(), buffer.begin() + remaining);
			d.input_buffer = std::move(buffer);
			d.buffer_begin = 0;
			count(&d.stats.copied_input_bytes, remaining);
		} else {
			// Releasing consumed input only moves a ring index, never the live suffix.
			d.buffer_begin = ring_index(d.ptr);
			size_t index = ring_index(remaining);
			size_t first = std::min(in.size(), capacity - index);
			std::copy_n(in.data(), first, d.input_buffer->data() + index);
			std::copy_n(in.data() + first, in.size() - first, d.input_buffer->data());
		}
		d.buffer_size = size;
		count(&d.stats.copied_input_bytes, in.size());
		d.source = { };
		d.input_offset += d.ptr;
		d.ptr = 0;
		d.not_enough_input = false;
	}
	void finish_input()
	{
		if (d.state != Error) d.input_finished = true;
	}
	void input_error(std::string message)
	{
		fail(ErrorCode::InputError, d.input_offset + input_size(), std::move(message));
	}
	int depth() const
	{
		return (int)d.tag_stack.size();
	}
	void hold()
	{
		if (d.advancing) {
			fail(ErrorCode::InvalidOperation, d.input_offset + d.ptr, "Cannot hold while advancing");
			return;
		}
		d.hold = true;
	}
	void nest()
	{
		if (d.advancing) {
			fail(ErrorCode::InvalidOperation, d.input_offset + d.ptr, "Cannot nest while advancing");
			return;
		}
		if (d.state == Error || d.result == ReadResult::EndDocument) return;
		if (d.depth_stack.size() >= limits_.max_depth || d.depth_stack.size() == d.depth_stack.max_size()) {
			exceeded("Maximum nested reader scope count exceeded", d.input_offset + d.ptr);
			return;
		}
		d.depth_stack.push_back(depth());
	}

	bool next()
	{
		return advance() == ReadResult::Event;
	}
	ReadResult advance()
	{
		if (d.advancing) {
			return fail(ErrorCode::InvalidOperation, d.input_offset + d.ptr, "Cannot reenter XML parsing");
		}
		if (d.state == Error) return d.result = ReadResult::Error;
		if (d.result == ReadResult::EndDocument) return d.result;
		detail::FlagGuard guard(&d.advancing);
		d.result = advance_internal();
		d.not_enough_input = d.result == ReadResult::NeedInput;
		return d.result;
	}
	ReadResult result() const
	{
		return d.result;
	}
	ErrorInfo const &error() const
	{
		return d.error;
	}

private:
	ReadResult advance_internal()
	{
		if (d.hold) {
			d.hold = false;
			if (d.state != None) return ReadResult::Event;
		}
		while (1) {
			ReadResult result = read_event();
			if (result == ReadResult::NeedInput && d.fn_input_calback) {
				// The scanner's views have expired before the callback can mutate input.
				size_t received = d.input_offset + input_size();
				d.fn_input_calback();
				if (d.state == Error) return ReadResult::Error;
				if (d.input_finished || d.input_offset + input_size() != received) continue;
				return ReadResult::NeedInput;
			}
			if (result != ReadResult::Event || d.depth_stack.empty()) return result;
			int effective_depth = depth();
			if (d.state == EndElement) --effective_depth;
			if (effective_depth >= d.depth_stack.back()) return ReadResult::Event;
			d.depth_stack.pop_back();
			d.hold = true;
			return ReadResult::SubtreeEnd;
		}
	}
	ReadResult finish_token(StateType state)
	{
		d.ptr += d.scan.cursor;
		d.scan = { };
		d.state = state;
		return ReadResult::Event;
	}
	ReadResult commit_characters(CharPart::Type type, size_t begin, size_t size)
	{
		if (type != CharPart::Comment) {
			if (!check_text(size, d.input_offset + d.ptr + begin)) return ReadResult::Error;
			CharPart part;
			part.type_ = type;
			part.sv_ = copy_input(d.ptr + begin, size);
			if (d.tag_stack.size() == 1) {
				d.outside_characters.emplace(std::move(part));
			} else {
				d.tag_stack.back().chars.chars_.push_back(std::move(part));
				d.tag_stack.back().text_bytes += size;
				d.stats.retained_text_bytes += size;
				++d.stats.retained_text_parts;
			}
		}
		return finish_token(type == CharPart::Comment ? Comment : Characters);
	}
	void add_attribute(size_t value_end)
	{
		auto *scan = &d.scan;
		scan->attributes.push_back({ scan->key_begin, scan->key_end, scan->value_begin, value_end });
		scan->attribute_bytes += (scan->key_end - scan->key_begin) + (value_end - scan->value_begin);
	}
	int compare_attribute_names(AttributeRange const &a, AttributeRange const &b) const
	{
		size_t a_size = a.key_end - a.key_begin;
		size_t b_size = b.key_end - b.key_begin;
		for (size_t i = 0; i < std::min(a_size, b_size); ++i) {
			unsigned char ac = input_byte(d.ptr + a.key_begin + i);
			unsigned char bc = input_byte(d.ptr + b.key_begin + i);
			if (ac != bc) return ac < bc ? -1 : 1;
		}
		return a_size == b_size ? 0 : (a_size < b_size ? -1 : 1);
	}
	bool check_unique_attributes()
	{
		if (d.scan.attributes.size() < 2) return true;
		// Sort only offsets; names and values remain in the input (including rings).
		// Avoid an all-pairs duplicate search without changing attribute order.
		auto sorted = d.scan.attributes;
		std::sort(sorted.begin(), sorted.end(), [&](AttributeRange const &a, AttributeRange const &b) {
			int cmp = compare_attribute_names(a, b);
			return cmp < 0 || (cmp == 0 && a.key_begin < b.key_begin);
		});
		size_t duplicate = std::numeric_limits<size_t>::max();
		for (size_t i = 1; i < sorted.size(); ++i) {
			if (compare_attribute_names(sorted[i - 1], sorted[i]) == 0) {
				duplicate = std::min(duplicate, sorted[i].key_begin);
			}
		}
		if (duplicate == std::numeric_limits<size_t>::max()) return true;
		fail(ErrorCode::InvalidSyntax, d.input_offset + d.ptr + duplicate, "Duplicate attribute name");
		return false;
	}
	bool check_xml_encoding()
	{
		auto const &scan = d.scan;
		for (auto const &attribute : scan.attributes) {
			if (copy_input(d.ptr + attribute.key_begin, attribute.key_end - attribute.key_begin) != "encoding") continue;
			std::string value = copy_input(d.ptr + attribute.value_begin, attribute.value_end - attribute.value_begin);
			size_t offset = d.input_offset + d.ptr + (attribute.value_begin ? attribute.value_begin : attribute.key_begin);
			auto letter = [](unsigned char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); };
			if (value.empty() || !letter((unsigned char)value[0])
				|| !std::all_of(value.begin(), value.end(), [&](unsigned char c) {
					   return letter(c) || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
				   })) {
				fail(ErrorCode::InvalidSyntax, offset, "Invalid XML encoding name");
				return false;
			}
			for (size_t i = 0; i < value.size(); ++i) {
				if (value[i] >= 'A' && value[i] <= 'Z') value[i] += 'a' - 'A';
			}
			if (value != "utf-8") {
				fail(ErrorCode::UnsupportedEncoding, offset, "Only UTF-8 XML input is supported");
				return false;
			}
		}
		return true;
	}
	ReadResult commit_tag()
	{
		auto *scan = &d.scan;
		size_t offset = d.input_offset + d.ptr;
		if (scan->marker != '/' && !check_unique_attributes()) return ReadResult::Error;
		std::string name = copy_input(d.ptr + scan->name_begin, scan->name_end - scan->name_begin);
		if (name == "?xml" && !check_xml_encoding()) return ReadResult::Error;
		if (scan->marker == '/') {
			if (d.tag_stack.size() <= 1 || name != d.tag_stack.back().name) {
				return fail(ErrorCode::MismatchedEndTag, offset, "End tag does not match the open element");
			}
			d.element_name.swap(name);
			return finish_token(EndElement);
		}
		bool declaration = scan->marker == '?';
		if (!declaration && d.tag_stack.size() == 1 && d.root_seen) {
			return fail(ErrorCode::InvalidSyntax, offset, "Multiple root elements");
		}
		// These checks precede any event-value allocation or stack growth.
		if ((!declaration && d.tag_stack.size() > limits_.max_depth)
			|| d.tag_stack.size() >= size_t(std::numeric_limits<int>::max())
			|| d.tag_stack.size() == d.tag_stack.max_size()) {
			exceeded("Maximum element depth exceeded", offset);
			return ReadResult::Error;
		}
		if (!fits(d.path.size(), 1, limits_.max_path_bytes)
			|| !fits(d.path.size() + 1, name.size(), limits_.max_path_bytes)
			|| !fits(d.path.size() + 1, name.size(), d.path.max_size())) {
			exceeded("Maximum path size exceeded", offset);
			return ReadResult::Error;
		}
		if (!check_attribute_bytes(0, 0)) return ReadResult::Error;
		Tag tag;
		tag.name = name;
		tag.parent_path_size = d.path.size();
		tag.attribute_bytes = scan->attribute_bytes;
		for (auto const &attribute : scan->attributes) {
			tag.atts.emplace_back(copy_input(d.ptr + attribute.key_begin, attribute.key_end - attribute.key_begin),
				copy_input(d.ptr + attribute.value_begin, attribute.value_end - attribute.value_begin));
		}
		size_t new_path_size = d.path.size() + 1 + name.size();
		if (new_path_size > d.path.capacity()) {
			d.path.reserve(grown_capacity(d.path.capacity(), new_path_size,
				std::min(limits_.max_path_bytes, d.path.max_size())));
		}
		d.tag_stack.push_back(std::move(tag));
		d.path.push_back('/');
		d.path.append(name);
		count(&d.stats.appended_path_bytes, name.size() + 1);
		d.stats.retained_attribute_bytes += scan->attribute_bytes;
		d.element_name.swap(name);
		if (!declaration && d.tag_stack.size() == 2) d.root_seen = true;
		d.next_end_element = scan->empty_element;
		return finish_token(declaration ? Declaration : StartElement);
	}
	ReadResult read_event()
	{
		if (d.state == Error) return ReadResult::Error;
		d.outside_characters.reset();
		if (d.state == EndElement || d.state == Declaration) {
			if (d.tag_stack.size() > 1) pop_tag();
		}
		d.state = None;
		if (d.next_end_element) {
			d.next_end_element = false;
			d.state = EndElement;
			return ReadResult::Event;
		}
		size_t offset = d.input_offset + d.ptr;
		size_t available = input_size() - d.ptr;
		while (d.scan.cursor < available) {
			char c = input_byte(d.ptr + d.scan.cursor);
			if (offset == 0 && d.scan.cursor == 0 && ((unsigned char)c == 0xff || (unsigned char)c == 0xfe)) {
				if (available < 2 && !d.input_finished) return ReadResult::NeedInput;
				if (available >= 2 && (unsigned char)input_byte(1) == ((unsigned char)c == 0xff ? 0xfe : 0xff)) {
					return fail(ErrorCode::UnsupportedEncoding, 0, "UTF-16 input is not supported");
				}
			}
			unsigned int codepoint = 0;
			size_t width = 0;
			auto utf8 = detail::read_utf8(available - d.scan.cursor, [&](size_t i) { return (unsigned char)input_byte(d.ptr + d.scan.cursor + i); }, &codepoint, &width);
			if (utf8 == detail::Utf8Result::Incomplete && !d.input_finished) return ReadResult::NeedInput;
			if (utf8 != detail::Utf8Result::Complete) {
				return fail(ErrorCode::InvalidUtf8, offset + d.scan.cursor, "Invalid or truncated UTF-8 sequence");
			}
			if (!detail::xml_char(codepoint)) {
				return fail(ErrorCode::InvalidCharacter, offset + d.scan.cursor, "Character is not allowed in XML 1.0");
			}
			d.scan.character_bytes = width;
			count(&d.stats.scan_steps);
			if (offset == 0 && d.scan.cursor == 0 && codepoint == 0xfeff) {
				d.ptr += width;
				offset += width;
				available -= width;
				continue; // A leading UTF-8 BOM is an encoding signature, not text.
			}
			switch (d.scan.phase) {
			case Phase::Start:
				if (c == '<') {
					if (!consume()) return ReadResult::Error;
					d.scan.phase = Phase::AfterLt;
				} else {
					d.scan.phase = Phase::Text;
				}
				break;
			case Phase::Text:
				if (c == '<') {
					if (d.scan.reference.active) return bad_reference();
					return commit_characters(CharPart::Text, 0, d.scan.cursor);
				}
				if (d.tag_stack.size() == 1 && !is_space(c)) {
					return fail(ErrorCode::InvalidSyntax, offset + d.scan.cursor, "Text outside the root element");
				}
				if (!check_reference(codepoint) || !check_text(d.scan.cursor + width, offset + d.scan.cursor)
					|| !consume()) return ReadResult::Error;
				break;
			case Phase::AfterLt:
				if (c == '!' || c == '?' || c == '/') {
					d.scan.marker = c;
					if (!consume()) return ReadResult::Error;
					d.scan.name_begin = c == '/' ? d.scan.cursor : 1;
					d.scan.phase = c == '!' ? Phase::Bang : Phase::NameFirst;
				} else {
					d.scan.name_begin = 1;
					d.scan.phase = Phase::NameFirst;
				}
				break;
			case Phase::Bang:
				if (c == '-' || c == '[') {
					if (!consume()) return ReadResult::Error;
					d.scan.phase = c == '-' ? Phase::CommentDash : Phase::CdataPrefix;
				} else
					return fail(ErrorCode::UnsupportedDeclaration, offset, "Unsupported markup declaration");
				break;
			case Phase::CommentDash:
				if (c != '-') return fail(ErrorCode::InvalidSyntax, offset + d.scan.cursor, "Invalid comment prefix");
				if (!consume()) return ReadResult::Error;
				d.scan.content_begin = d.scan.cursor;
				d.scan.phase = Phase::Comment;
				break;
			case Phase::CdataPrefix:
				if (c != std::string_view("CDATA[")[d.scan.matched]) {
					return fail(ErrorCode::InvalidSyntax, offset + d.scan.cursor, "Invalid CDATA prefix");
				}
				if (!consume()) return ReadResult::Error;
				if (++d.scan.matched == 6) {
					if (d.tag_stack.size() == 1) return fail(ErrorCode::InvalidSyntax, offset, "CDATA outside the root element");
					d.scan.matched = 0;
					d.scan.content_begin = d.scan.cursor;
					d.scan.phase = Phase::Cdata;
					if (!check_text(0, offset + d.scan.cursor)) return ReadResult::Error;
				}
				break;
			case Phase::Comment:
			case Phase::Cdata: {
				bool cdata = d.scan.phase == Phase::Cdata;
				if (!cdata && !allow_invalid_comments_ && d.scan.matched == 2 && c != '>') {
					return fail(ErrorCode::InvalidSyntax, offset + d.scan.cursor,
						"Double hyphen is only allowed in the comment closing delimiter");
				}
				bool complete = c == '>' && d.scan.matched == 2;
				if (!consume()) return ReadResult::Error;
				if (complete) {
					return commit_characters(cdata ? CharPart::CDATA : CharPart::Comment,
						d.scan.content_begin, d.scan.cursor - d.scan.content_begin - 3);
				}
				d.scan.matched = c == (cdata ? ']' : '-') ? std::min(size_t(2), d.scan.matched + 1) : 0;
				if (cdata && !check_text(d.scan.cursor - d.scan.content_begin - d.scan.matched, offset + d.scan.cursor - 1)) {
					return ReadResult::Error;
				}
				break;
			}
			case Phase::NameFirst:
				if (!detail::name_start(codepoint)) return fail(ErrorCode::InvalidSyntax, offset + d.scan.cursor, "Invalid element name");
				if ((d.scan.marker == 0 && d.tag_stack.size() > limits_.max_depth)
					|| (d.scan.marker != '/' && d.tag_stack.size() >= size_t(std::numeric_limits<int>::max()))) {
					exceeded("Maximum element depth exceeded", offset);
					return ReadResult::Error;
				}
				if (!check_name() || !consume()) return ReadResult::Error;
				d.scan.phase = Phase::Name;
				break;
			case Phase::Name:
				if (detail::name_char(codepoint)) {
					if (!check_name() || !consume()) return ReadResult::Error;
				} else {
					d.scan.name_end = d.scan.cursor;
					d.scan.phase = d.scan.marker == '/' ? Phase::EndSpace : Phase::TagGap;
				}
				break;
			case Phase::EndSpace:
				if (is_space(c)) {
					if (!consume()) return ReadResult::Error;
				} else if (c == '>') {
					if (!consume()) return ReadResult::Error;
					return commit_tag();
				} else {
					return fail(ErrorCode::InvalidSyntax, offset + d.scan.cursor, "Expected > after end tag");
				}
				break;
			case Phase::TagGap: {
				bool declaration = d.scan.marker == '?';
				if (is_space(c)) {
					if (!consume()) return ReadResult::Error;
					d.scan.separated = true;
				} else if (c == '>' && d.scan.marker != '?') {
					if (!consume()) return ReadResult::Error;
					return commit_tag();
				} else if ((c == '?' && d.scan.marker == '?') || (c == '/' && !declaration)) {
					if (!consume()) return ReadResult::Error;
					d.scan.empty_element = !declaration;
					d.scan.phase = Phase::TagBracket;
				} else {
					if (!d.scan.separated || !detail::name_start(codepoint)) {
						return fail(ErrorCode::InvalidSyntax, offset + d.scan.cursor, "Expected an attribute or tag terminator");
					}
					if (d.scan.attributes.size() >= limits_.max_attributes || d.scan.attributes.size() == d.scan.attributes.max_size()
						|| d.scan.attributes.size() == d.tag_stack.back().atts.max_size()) {
						exceeded("Maximum attribute count exceeded", offset + d.scan.cursor);
						return ReadResult::Error;
					}
					d.scan.key_begin = d.scan.cursor;
					d.scan.value_begin = 0;
					d.scan.phase = Phase::AttributeName;
				}
				break;
			}
			case Phase::AttributeName:
				if (detail::name_char(codepoint)) {
					size_t size = d.scan.cursor - d.scan.key_begin;
					if (!fits(size, width, std::min(limits_.max_name_bytes, std::string().max_size()))) {
						exceeded("Maximum attribute name size exceeded", offset + d.scan.cursor);
						return ReadResult::Error;
					}
					if (!check_attribute_bytes(size + width, 0) || !consume()) return ReadResult::Error;
				} else {
					d.scan.key_end = d.scan.cursor;
					d.scan.separated = false;
					d.scan.phase = Phase::AttributeEquals;
				}
				break;
			case Phase::AttributeEquals:
				if (is_space(c)) {
					if (!consume()) return ReadResult::Error;
					d.scan.separated = true;
				} else if (c == '=') {
					if (!consume()) return ReadResult::Error;
					d.scan.phase = Phase::AttributeQuote;
				} else if (d.scan.marker == '?') {
					add_attribute(0);
					d.scan.phase = Phase::TagGap;
				} else {
					return fail(ErrorCode::InvalidSyntax, offset + d.scan.cursor, "Expected = after attribute name");
				}
				break;
			case Phase::AttributeQuote:
				if (is_space(c)) {
					if (!consume()) return ReadResult::Error;
				} else {
					if (c != '\'' && c != '"') {
						return fail(ErrorCode::InvalidSyntax, offset + d.scan.cursor, "Expected a quoted attribute value");
					}
					d.scan.quote = c;
					if (!consume()) return ReadResult::Error;
					d.scan.value_begin = d.scan.cursor;
					d.scan.phase = Phase::AttributeValue;
				}
				break;
			case Phase::AttributeValue:
				if (c == d.scan.quote) {
					if (d.scan.reference.active) return bad_reference();
					size_t end = d.scan.cursor;
					if (!consume()) return ReadResult::Error;
					add_attribute(end);
					d.scan.separated = false;
					d.scan.phase = Phase::TagGap;
				} else {
					if (c == '<' && d.scan.reference.active) return bad_reference();
					if (c == '<') return fail(ErrorCode::InvalidSyntax, offset + d.scan.cursor, "Unexpected < in attribute value");
					size_t size = d.scan.cursor - d.scan.value_begin;
					if (!fits(size, width, std::min(limits_.max_attribute_value_bytes, std::string().max_size()))) {
						exceeded("Maximum attribute value size exceeded", offset + d.scan.cursor);
						return ReadResult::Error;
					}
					if (!check_reference(codepoint) || !check_attribute_bytes(d.scan.key_end - d.scan.key_begin, size + width) || !consume()) {
						return ReadResult::Error;
					}
				}
				break;
			case Phase::TagBracket:
				if (c != '>') return fail(ErrorCode::InvalidSyntax, offset + d.scan.cursor, "Expected >");
				if (!consume()) return ReadResult::Error;
				return commit_tag();
			}
		}
		if (d.scan.phase != Phase::Start) {
			if (d.input_finished && d.scan.reference.active) return bad_reference();
			if (d.input_finished && d.scan.phase == Phase::Text) return commit_characters(CharPart::Text, 0, d.scan.cursor);
			return incomplete();
		}
		if (!d.input_finished) return ReadResult::NeedInput;
		if (!d.root_seen || d.tag_stack.size() != 1) return incomplete();
		return ReadResult::EndDocument;
	}

public:
	StateType state() const
	{
		return d.state;
	}
	bool is_not_enough_input() const
	{
		return d.not_enough_input;
	}
	bool is_declaration() const
	{
		return state() == Declaration;
	}
	bool is_start_element() const
	{
		return state() == StartElement;
	}
	bool is_end_element() const
	{
		return state() == EndElement;
	}
	bool is_start_element(char const *name) const
	{
		return is_start_element() && is_element_name(name);
	}
	bool is_end_element(char const *name) const
	{
		return is_end_element() && is_element_name(name);
	}
	bool is_start_element(std::string_view name) const
	{
		return is_start_element() && is_element_name(name);
	}
	bool is_end_element(std::string_view name) const
	{
		return is_end_element() && is_element_name(name);
	}
	bool is_characters() const
	{
		return state() == Characters;
	}
	std::string const &path() const
	{
		return current_path();
	}
	bool match_start(char const *path) const
	{
		return (is_start_element() || is_declaration()) && match_internal(path);
	}
	bool match_end(char const *path) const
	{
		return is_end_element() && match_internal(path);
	}
	std::string name() const
	{
		return std::string(d.element_name);
	}
	bool is_name(char const *s) const
	{
		size_t n = d.element_name.size();
		for (size_t i = 0; i < n; i++) {
			if (s[i] != d.element_name[i]) return false;
		}
		return s[n] == 0;
	}
	std::string text() const
	{
		return encoded_chars().to_string();
	}
	CharPart characters() const
	{
		// Comments do not retain body bytes or add accumulated text fragments.
		if (d.state == Comment) {
			CharPart part;
			part.type_ = CharPart::Comment;
			return part;
		}
		// The returned part owns its data, independently of subsequent input.
		if (d.outside_characters) return *d.outside_characters;
		assert(!d.tag_stack.empty());
		if (d.tag_stack.back().chars.chars_.empty()) return { };
		return d.tag_stack.back().chars.chars_.back();
	}
	std::optional<EscapedAttributeValue> attribute(std::string_view name) const
	{
		assert(!d.tag_stack.empty());
		for (auto const &attr : d.tag_stack.back().atts) {
			if (attr.first == name) {
				return attr.second;
			}
		}
		return std::nullopt;
	}
	std::string attribute(std::string_view name, std::string const &defval) const
	{
		auto s = attribute(name);
		return s ? (std::string)*s : defval;
	}
	std::vector<std::pair<std::string, EscapedAttributeValue>> attributes() const
	{
		std::vector<std::pair<std::string, EscapedAttributeValue>> ret;
		assert(!d.tag_stack.empty());
		for (auto const &attr : d.tag_stack.back().atts) {
			ret.emplace_back(std::string(attr.first), attr.second);
		}
		return ret;
	}
}; // class Reader

class Writer {
private:
	std::function<int(char const *p, int n)> fn_writer;
	std::vector<char> line_;
	bool inside_tag_ = false;
	size_t newline_ = false;
	size_t indent_step_ = 4;
	struct Element {
		std::string name;
		bool has_text = false;
	};
	std::vector<Element> element_stack_;
	xstream::ErrorInfo error_;
	bool writing_ = false;
	bool started_ = false;
	size_t written_bytes_ = 0;
	bool fail(xstream::ErrorCode code, size_t offset, std::string message)
	{
		if (good()) error_ = { code, offset, std::move(message) };
		return false;
	}
	bool ready(bool start_operation = true)
	{
		if (!good()) return false;
		if (writing_) return fail(ErrorCode::InvalidOperation, written_bytes_, "Cannot reenter an output callback");
		if (start_operation) started_ = true;
		return true;
	}
	bool emit(char const *p, size_t size)
	{
		while (size != 0) {
			size_t available = std::numeric_limits<size_t>::max() - written_bytes_;
			if (available == 0) return fail(ErrorCode::LimitExceeded, written_bytes_, "Output byte count exceeded");
			int count = int(std::min({ size, available, size_t(std::numeric_limits<int>::max()) }));
			int result;
			{
				detail::FlagGuard guard(&writing_);
				result = fn_writer(p, count);
			}
			if (result > 0 && result <= count) written_bytes_ += size_t(result);
			if (!good()) return false; // The callback may have called output_error().
			if (result <= 0 || result > count) return fail(ErrorCode::OutputError, written_bytes_, "Output callback failed or returned an invalid byte count");
			p += result;
			size -= size_t(result);
		}
		return true;
	}
	void write_line(std::string_view s)
	{
		line_.insert(line_.end(), s.begin(), s.end());
	}
	bool write_indent(size_t n)
	{
		if (indent_step_ == 0 || !newline_) return true;
		size_t available = line_.max_size() - line_.size();
		if (available == 0 || n > (available - 1) / indent_step_) {
			return fail(ErrorCode::LimitExceeded, written_bytes_, "Indentation is too large");
		}
		n = n * indent_step_ + 1;
		line_.insert(line_.begin(), n, ' ');
		line_[0] = '\n';
		return true;
	}
	bool flush()
	{
		if (!good()) return false;
		if (!line_.empty()) {
			if (!emit(line_.data(), line_.size())) return false;
			line_.clear();
		}
		return true;
	}
	void close_tag()
	{
		if (inside_tag_) {
			write_line(">");
			inside_tag_ = false;
		}
	}

public:
	using ErrorCode = xstream::ErrorCode;
	using ErrorInfo = xstream::ErrorInfo;
	static constexpr size_t max_indent_width = 64;
	Writer(std::function<int(char const *p, int n)> fn_writer)
		: fn_writer(fn_writer)
	{
		if (!this->fn_writer) fail(ErrorCode::InvalidOperation, 0, "Output callback is not configured");
	}
	bool good() const { return error_.code == ErrorCode::None; }
	ErrorInfo const &error() const { return error_; }
	size_t indent_width() const { return indent_step_; }
	bool set_indent_width(size_t width)
	{
		if (!ready(false)) return false;
		if (started_) return fail(ErrorCode::InvalidOperation, written_bytes_, "Indent width can only change before writing");
		if (width > max_indent_width) return fail(ErrorCode::InvalidOperation, written_bytes_, "Indent width must be between 0 and 64");
		indent_step_ = width;
		return true;
	}
	void output_error(std::string message)
	{
		fail(ErrorCode::OutputError, written_bytes_, std::move(message));
	}
	bool start_document()
	{
		if (!ready()) return false;
		write_line("<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
		if (indent_step_ != 0) write_line("\n");
		return true;
	}
	bool end_document()
	{
		if (!ready()) return false;
		while (!element_stack_.empty()) {
			if (!end_element()) return false;
		}
		if (!flush()) return false;
		if (indent_step_ != 0 && newline_) return emit("\n", 1);
		return true;
	}
	bool start_element(std::string const &name)
	{
		if (!ready()) return false;
		close_tag();
		if (!flush()) return false;
		if ((element_stack_.empty() || !element_stack_.back().has_text) && !write_indent(element_stack_.size())) return false;
		element_stack_.push_back({ name, false });
		write_line("<");
		write_line(name);
		inside_tag_ = true;
		newline_ = true;
		return true;
	}
	bool end_element()
	{
		if (!ready()) return false;
		if (element_stack_.empty()) return true;
		std::string name = element_stack_.back().name;
		bool has_text = element_stack_.back().has_text;
		if (inside_tag_) {
			write_line(" />");
			element_stack_.pop_back();
			inside_tag_ = false;
		} else {
			close_tag();
			if (!flush()) return false;
			element_stack_.pop_back();
			if (!has_text && !write_indent(element_stack_.size())) return false;
			write_line("</");
			write_line(name);
			write_line(">");
		}
		newline_ = true;
		return flush();
	}
	bool write_characters(std::string_view s)
	{
		if (!ready()) return false;
		std::string encoded;
		ErrorInfo error;
		if (!html_encode(s, &encoded, &error)) {
			error_ = std::move(error);
			return false;
		}
		close_tag();
		if (!element_stack_.empty()) element_stack_.back().has_text = true;
		newline_ = false;
		write_line(encoded);
		return true;
	}
	bool write_attribute(std::string_view name, std::string_view value)
	{
		if (!ready()) return false;
		if (!inside_tag_) return fail(ErrorCode::InvalidOperation, written_bytes_, "Attributes require an open start tag");
		std::string encoded;
		ErrorInfo error;
		if (!detail::encode_characters(value, &encoded, &error, true, true)) {
			error_ = std::move(error);
			return false;
		}
		write_line(" ");
		write_line(name);
		write_line("=\"");
		write_line(encoded);
		write_line("\"");
		return true;
	}
	bool element(std::string const &name, std::function<void()> fn)
	{
		if (!start_element(name)) return false;
		if (fn) fn();
		return end_element();
	}
	bool text_element(std::string const &name, std::string const &text)
	{
		return start_element(name) && write_characters(text) && end_element();
	}
}; // class Writer

} // namespace xstream
#endif // XSTREAM_H
