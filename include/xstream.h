// Xstream - Header-only Streaming pull-based XML/HTML Parser and Generator
// Copyright (C) 2026 S.Fuchita (soramimi)
// This software is distributed under the MIT license.

#ifndef XSTREAM_H
#define XSTREAM_H

#include <assert.h>
#include <cstdio>
#include <cstring>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// #include "htmlencode.h"

namespace xstream {

#ifdef __HTMLENCODE_H

inline std::string html_encode(std::string_view str)
{
	return ::html_encode(str, true);

}

inline std::string html_decode(std::string_view str)
{
	return ::html_decode(str);
}

#else

static inline void vecprint(std::vector<char> *out, char c)
{
	out->push_back(c);
}

static inline void vecprint(std::vector<char> *out, char const *s)
{
	out->insert(out->end(), s, s + strlen(s));
}

static inline void html_encode_(char const *ptr, char const *end, bool utf8through, std::vector<char> *vec)
{
	while (ptr < end) {
		int c = *ptr & 0xff;
		ptr++;
		switch (c) {
		case '&':
			vecprint(vec, "&amp;");
			break;
		case '<':
			vecprint(vec, "&lt;");
			break;
		case '>':
			vecprint(vec, "&gt;");
			break;
		case '\"':
			vecprint(vec, "&quot;");
			break;
		case '\'':
			vecprint(vec, "&apos;");
			break;
		case '\t':
		case '\n':
			vecprint(vec, c);
			break;
		default:
			if (c < 0x80 ? (c < 0x20 || c == '\'') : !utf8through) {
				char tmp[10];
				sprintf(tmp, "&#%u;", c);
				vecprint(vec, tmp);
			} else {
				vecprint(vec, c);
			}
		}
	}
}

static inline std::string html_encode(std::string_view str, bool utf8through = true)
{
	char const *begin = str.data();
	char const *end = begin + str.size();
	char const *ptr = begin;
	while (ptr < end) {
		int c = (unsigned char)*ptr;
		if (isspace(c) || strchr("&<>\"\'", c)) {
			break;
		}
		ptr++;
	}
	if (ptr == end) {
		return (std::string)str;
	}
	std::vector<char> vec;
	vec.reserve(str.size() * 2);
	vec.insert(vec.end(), begin, ptr);
	html_encode_(ptr, end, utf8through, &vec);
	begin = &vec[0];
	end = begin + vec.size();
	return std::string(begin, end);
}

static inline void html_decode_(char const *ptr, char const *end, std::vector<char> *vec)
{
	while (ptr < end) {
		int c = *ptr & 0xff;
		ptr++;
		if (c == '&') {
			char const *next = strchr(ptr, ';');
			if (!next) {
				break;
			}
			std::string t(ptr, next);
			if (t[0] == '#') {
				c = atoi(t.c_str() + 1);
				vecprint(vec, c);
			} else if (t == "amp") {
				vecprint(vec, '&');
			} else if (t == "lt") {
				vecprint(vec, '<');
			} else if (t == "gt") {
				vecprint(vec, '>');
			} else if (t == "quot") {
				vecprint(vec, '\"');
			} else if (t == "apos") {
				vecprint(vec, '\'');
			}
			ptr = next + 1;
		} else {
			vecprint(vec, c);
		}
	}
}

static inline std::string html_decode(std::string_view str)
{
	char const *begin = str.data();
	char const *end = begin + str.size();
	char const *ptr = begin;
	while (ptr < end) {
		int c = (unsigned char)*ptr;
		if (c == '&') {
			break;
		}
		ptr++;
	}
	if (ptr == end) {
		return (std::string)str;
	}
	std::vector<char> vec;
	vec.reserve(str.size() * 2);
	vec.insert(vec.end(), begin, ptr);
	html_decode_(ptr, end, &vec);
	begin = &vec[0];
	end = begin + vec.size();
	return std::string(begin, end);
}

#endif // __HTMLENCODE_H

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
			return {vec.data(), vec.size()};
		}
		return {};
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
private:
	struct CharPart {
		enum Type {
			Text,
			CDATA,
			Comment,
		};
		Type type_ = Text;
		std::string_view sv_;
		CharPart() = default;
		CharPart(Type type, char const *begin, char const *end)
			: type_(type)
			, sv_(begin, end - begin)
		{
		}
		Type type() const
		{
			return type_;
		}
		std::vector<char> decode() const
		{
			if (type_ == Text) {
				std::vector<char> v;
				html_decode_(sv_.data(), sv_.data() + sv_.size(), &v);
				return v;
			} else if (type_ == CDATA) {
				return std::vector<char>(sv_.data(), sv_.data() + sv_.size());
			}
			return {};
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
		EncodedCharacters()
		{
			chars_.reserve(16);
		}
		void append(EncodedCharacters &&a)
		{
			EncodedCharacters b = std::move(a);
			for (CharPart &f : b.chars_) {
				chars_.push_back(std::move(f));
			}
		}
		std::string to_string() const
		{
			std::vector<CharPart> const &parts = chars_;
			std::vector<char> vec;
			size_t len = 0;
			for (auto &part : parts) {
				len += part.sv_.size();
			}
			vec.reserve(len + 100);
			for (auto &part : parts) {
				std::vector<char> v = part.decode();
				if (!v.empty()) {
					vec.insert(vec.end(), v.begin(), v.end());
				}
			}
			return std::string(vec.data(), vec.size());
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
		operator std::string () const
		{
			return to_string();
		}
	};
	struct Tag {
		std::string path;
		std::vector<std::pair<std::string, std::string>> atts;
		EncodedCharacters chars;
		Tag() = default;
		Tag(std::string const &path)
			: path(path)
		{
		}
	};
	struct D {
		bool not_enough_input = false;
		bool acceptable = false;
		std::optional<std::vector<char>> input_buffer;
		std::function<void ()> fn_input_calback;
		
		std::vector<int> depth_stack;
		bool hold = false;
		std::vector<Tag> tag_stack;
		std::string last_path;
		char const *begin = nullptr;
		char const *end = nullptr;
		char const *ptr = nullptr;
		char const *chars = nullptr;
		StateType state = None;
		bool next_end_element = false;
		std::string_view element_name;
	} d;
private:
	bool is_streaming_input_mode() const
	{
		return (bool)d.fn_input_calback;
	}
	void need_input() const
	{
		if (is_streaming_input_mode()) {
			d.fn_input_calback();
		}
	}
	int peek_next_char() const
	{
		if (d.ptr && d.end && d.ptr < d.end) {
			return (unsigned char)*d.ptr;
		}
		need_input();
		if (d.ptr && d.end && d.ptr < d.end) {
			return (unsigned char)*d.ptr;
		}
		return -1;
	}
	
	bool issymf(char c)
	{
		int d = (unsigned char)c;
		if (d < 0x100) {
			if (d >= 'a' && d <= 'z') return true;
			if (d >= 'A' && d <= 'Z') return true;
			if (d == '_') return true;
			if (d == ':') return true;
			if (d >= 0xc0 && d <= 0xd6) return true;
			if (d >= 0xd8 && d <= 0xf6) return true;
			if (d >= 0xf8) return true;
			return false;
		}
		return true;
	}
	bool issym(char c)
	{
		int d = (unsigned char)c;
		if (d < 0x100) {
			if (d >= '0' && d <= '9') return true;
			if (d == '-') return true;
			if (d == '.') return true;
			if (d == 0xb7) return true;
			return issymf(c);
		}
		return true;
	}
	void reset_stack()
	{
		d.tag_stack.clear();
		d.tag_stack.push_back({});
		d.last_path.clear();
	}
	void init(char const *begin, char const *end)
	{
		d.begin = begin;
		d.end = end;
		d.ptr = d.begin;
		reset_stack();
	}
	std::string const &current_path() const
	{
		if (!d.last_path.empty()) return d.last_path;
		if (d.tag_stack.empty()) return d.last_path; // empty string
		return d.tag_stack.back().path;
	}
	void pop_path()
	{
		if (!d.last_path.empty()) {
			d.last_path.clear();
		} else if (!d.tag_stack.empty()) {
			d.tag_stack.pop_back();
		}
	}
	bool match_internal(char const *path) const
	{
		std::string currpath = current_path();
		int n = currpath.size();
		if (strncmp(path, currpath.c_str(), n) == 0) {
			if (path[n] == 0) {
				return true;
			}
			if (path[n] == '/' && path[n + 1] == 0) {
				return true;
			}
		}
		return false;
	}
	void append_chars(CharPart::Type type, char const *begin, char const *end)
	{
		assert(!d.tag_stack.empty());
		d.tag_stack.back().chars.append(type, begin, end);
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
	Reader()
	{
	}
	Reader(char const *begin, char const *end)
	{
		init(begin, end);
	}
	
	Reader(char const *ptr, size_t len)
	{
		init(ptr, ptr + len);
	}

	Reader(std::string_view s)
	{
		d.begin = s.data();
		d.end = s.data() + s.size();
		init(d.begin, d.end);
	}
	void parse(std::function<void ()> fn_input_calback)
	{
		d = {};
		init(nullptr, nullptr);
		d.fn_input_calback = fn_input_calback;
	}
	void input(std::string_view in)
	{
		if (in.empty()) return;
		
		static constexpr size_t EXTRA_ROOM = 200; // reserve extra room to avoid frequent reallocations
		
		d.not_enough_input = false;
		
		if (d.input_buffer && (d.input_buffer->capacity() - d.input_buffer->size()) >= in.size()) {
			if (d.ptr && d.end && d.ptr == d.end) {
				// all previous input has been consumed, reuse the buffer
				d.input_buffer->assign(in.begin(), in.end());
				d.begin = d.ptr = d.input_buffer->data();
			} else {
				// append new input to the existing buffer
				d.input_buffer->insert(d.input_buffer->end(), in.begin(), in.end());
				if (!d.begin) d.begin = d.input_buffer->data();
				if (!d.ptr)   d.ptr = d.input_buffer->data();
			}
			d.end = d.input_buffer->data() + d.input_buffer->size();
		} else {
			std::vector<char> newbuf;
			size_t curr = (d.ptr && d.end) ? (d.end - d.ptr) : 0;
			newbuf.reserve(curr + in.size() + EXTRA_ROOM);
			if (curr > 0) {
				newbuf.assign(d.ptr, d.end); // copy remaining unprocessed data to the new buffer
			}
			newbuf.insert(newbuf.end(), in.begin(), in.end()); // append new input data
			d.input_buffer = std::move(newbuf);
			d.begin = d.input_buffer->data();
			d.ptr = d.begin;
			d.end = d.begin + d.input_buffer->size();
		}
	}
	int depth() const
	{
		return (int)d.tag_stack.size();
	}
	void hold()
	{
		d.hold = true;
	}
	void nest()
	{
		d.depth_stack.push_back(depth());
	}

	bool next()
	{
		if (d.hold) {
			d.hold = false;
			return true;
		}
		if (_internal_next()) {
			if (d.depth_stack.empty()) return true;
			int e = depth();
			if (d.state == EndElement && e > 0) {
				e--;
			}
			if (e >= d.depth_stack.back()) {
				return true;
			}
			d.depth_stack.pop_back();
			hold();
		}
		if (is_streaming_input_mode() && !is_not_enough_input()) {
			if (state() != EndElement) {
				return true;
			}
		}
		return false;
	}
	bool _internal_next()
	{
		assert(!d.tag_stack.empty()); // least one element
		d.acceptable = false;
		if (d.state == EndElement) {
			size_t i = d.tag_stack.size();
			while (i > 1) {
				i--;
				size_t s = d.tag_stack[i].path.size();
				size_t n = d.element_name.size();
				if (s > n) {
					if (d.tag_stack[i].path[s - n - 1] == '/' && memcmp(&d.tag_stack[i].path[s - n], d.element_name.data(), n) == 0) {
						d.tag_stack.resize(i);
						d.state = None;
						break;
					}
				}
			}
		} else if (d.state == Declaration) {
			if (d.tag_stack.size() > 1) {
				d.tag_stack.pop_back();
			} else {
				reset_stack();
			}
		}
		bool not_enough_input = false;
		while (1) {
			if (!d.chars) {
				d.chars = d.ptr;
			}
			if (d.next_end_element) {
				d.next_end_element = false;
				d.state = EndElement;
				return true;
			}
			if (d.ptr + 9 < d.end && *d.ptr == '<') {
				char const *p = d.ptr;
				if (memcmp(p, "<![CDATA[", 9) == 0) {
					p += 9;
					char const *left = p;
					while (p + 2 < d.end) {
						if (memcmp(p, "]]>", 3) == 0) {
							append_chars(CharPart::CDATA, left, p);
							p += 3;
							d.chars = nullptr;
							d.state = Characters;
							d.ptr = p;
							return true;
						}
						p++;
					}
					not_enough_input = true;
					break;
				}
			}
			if (d.ptr < d.end && *d.ptr == '<') {
				char const *p = d.ptr + 1;
				if (p + 3 < d.end && memcmp(p, "!--", 3) == 0) {
					p += 3;
					char const *left = p;
					while (p + 2 < d.end && memcmp(p, "-->", 3) != 0) {
						p++;
					}
					append_chars(CharPart::Comment, left, p);
					p += 3;
					d.chars = nullptr;
					d.state = Comment;
					d.ptr = p;
					return true;
				}
				char start = 0;
				if (p < d.end && *p == '/') {
					start = *p++;
				} else {
					if (p < d.end && (*p == '?' || *p == '!')) {
						start = *p;
					}
				}
				if (p < d.end && (issymf(*p) || start == '?' || start == '!')) {
					char const *left = p++;
					while (p < d.end && issym(*p)) {
						p++;
					}
					d.element_name = std::string_view(left, p - left);
					if (p == d.end) {
						not_enough_input = true;
						break;
					}
					std::vector<std::pair<std::string_view, std::string_view>> atts;
					while (p < d.end && isspace((unsigned char)*p)) {
						p++;
						while (p < d.end && isspace((unsigned char)*p)) {
							p++;
						}
						char const *eq = nullptr;
						char quote = 0;
						if (start != '/' && p < d.end && issymf(*p)) {
							char const *left = p++;
							p++;
							while (p < d.end) {
								if (!eq) {
									if (*p == '=') {
										eq = p;
										p++;
										if (p < d.end) {
											char q = *p;
											if (q == '\'' || q == '\"') {
												quote = q;
												p++;
											}
										}
									} else if (isspace(*p) || *p == '>' || *p == '/') {
										break;
									} else {
										p++;
									}
								} else if (quote != 0 ? (*p == quote) : (isspace((unsigned char)*p) || *p == '>' || *p == '/')) {
									if (*p == quote) {
										p++;
									}
									std::string_view key;
									std::string_view val;
									if (eq) {
										key = std::string_view(left, eq - left);
										char const *left = eq + 1;
										char const *right = p;
										if (left + 1 < right && left[0] == quote && right[-1] == quote) {
											left++;
											right--;
										}
										val = std::string_view(left, right - left);
									} else {
										key = std::string_view(left, p - left);
									}
									atts.emplace_back(key, val);
									break;
								} else {
									p++;
								}
							}
						} else {
							break;
						}
					}
					if (p < d.end && *p == '/') {
						p++;
						d.next_end_element = true;
					}
					if (start == '!' || (start == '?' && p < d.end && *p == '?')) {
						if (start == '?') {
							p++;
						}
						if (p < d.end && *p == '>') {
							p++;
							d.tag_stack.push_back(current_path() + '/' + std::string(d.element_name));
							// d.tag_stack.back().atts = std::move(atts);
							d.tag_stack.back().atts.clear();
							for (auto const &attr : atts) {
								d.tag_stack.back().atts.emplace_back(std::string(attr.first), std::string(attr.second));
							}
							d.state = Declaration;
							d.ptr = p;
							d.acceptable = true;
							return true;
						}
					}
					if (p < d.end && *p == '>') {
						p++;
						d.chars = nullptr;
						if (start == '/') {
							d.state = EndElement;
						} else {
							d.tag_stack.push_back(current_path() + '/' + std::string(d.element_name));
							d.tag_stack.back().atts.clear();
							for (auto const &attr : atts) {
								d.tag_stack.back().atts.emplace_back(std::string(attr.first), std::string(attr.second));
							}
							d.state = StartElement;
						}
						d.ptr = p;
						d.acceptable = true;
						return true;
					}
				}
				not_enough_input = true;
				break;
			} else if (d.ptr < d.end) {
				if (d.state == EndElement) {
					d.last_path = {};
				}
				char const *p = d.ptr;
				while (1) {
					int c = -1;
					if (p < d.end) {
						c = (unsigned char)*p;
					}
					if (c == -1) {
						d.chars = nullptr;
						not_enough_input = true;
						break;
					}
					if (c == '<') {
						append_chars(CharPart::Text, d.chars, p);
						d.chars = nullptr;
						d.state = Characters;
						d.ptr = p;
						return true;
					}
					p++;
				}
				break;
			} else {
				not_enough_input = true;
				break;
			}
		}
		if (not_enough_input) {
			d.not_enough_input = true;
			need_input();
			return false;
		}
		return false;
	}
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
		return d.acceptable && (is_start_element() || is_declaration()) && match_internal(path);
	}
	bool match_end(char const *path) const
	{
		return d.acceptable && is_end_element() && match_internal(path);
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
		assert(!d.tag_stack.empty());
		if (d.tag_stack.back().chars.chars_.empty()) return {};
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
	std::function<int (char const *p, int n)> fn_writer;
	std::vector<char> line_;
	bool inside_tag_ = false;
	size_t newline_ = false;
	int indent_step_ = 4;
	std::vector<std::string> element_stack_;
	void write_line(std::string_view s)
	{
		line_.insert(line_.end(), s.begin(), s.end());
	}
	void write_indent(size_t n)
	{
		n *= indent_step_;
		if (newline_) {
			n++;
		}
		line_.insert(line_.begin(), n, ' ');
		if (newline_) {
			line_[0] = '\n';
		}
	}
	void flush()
	{
		if (!line_.empty()) {
			fn_writer(line_.data(), line_.size());
			line_.clear();
		}
	}
	void close_tag()
	{
		if (inside_tag_) {
			write_line(">");
			inside_tag_ = false;
		}
	}
public:
	Writer(std::function<int (char const *p, int n)> fn_writer)
		: fn_writer(fn_writer)
	{
	}
	void start_document()
	{
		std::string s = R"---(<?xml version="1.0" encoding="UTF-8"?>)---" "\n";
		write_line(s);
	}
	void end_document()
	{
		while (!element_stack_.empty()) {
			end_element();
		}
		if (newline_) {
			char c = '\n';
			fn_writer(&c, 1);
		}
	}
	void start_element(std::string const &name)
	{
		close_tag();
		flush();
		write_indent(element_stack_.size());
		element_stack_.push_back(name);
		write_line("<");
		write_line(name);
		inside_tag_ = true;
		newline_ = true;
	}
	void end_element()
	{
		if (!element_stack_.empty()) {
			std::string name = element_stack_.back();
			if (inside_tag_) {
				write_line(" />");
				if (!element_stack_.empty()) {
					element_stack_.pop_back();
				}
				inside_tag_ = false;
			} else {
				close_tag();
				flush();
				element_stack_.pop_back();
				if (newline_) {
					write_indent(element_stack_.size());
				}
				write_line("</");
				write_line(name);
				write_line(">");
			}
			flush();
			newline_ = true;
		}
	}
	void write_characters(std::string_view s)
	{
		close_tag();
		newline_ = false;
		write_line(html_encode(s));
	}
	void write_attribute(std::string_view name, std::string_view value)
	{
		if (inside_tag_) {
			write_line(" ");
			write_line(name);
			write_line("=\"");
			write_line(html_encode(value));
			write_line("\"");
		}
	}
	void element(std::string const &name, std::function<void ()> fn)
	{
		start_element(name);
		if (fn) {
			fn();
		}
		end_element();
	}
	void text_element(std::string const &name, std::string const &text)
	{
		element(name, [&](){
			write_characters(text);
		});
	}
}; // class Writer

} // namespace xstream
#endif // XSTREAM_H
