// Xstream - Header-only Streaming pull-based XML/HTML Parser and Generator
// Copyright (C) 2025 S.Fuchita (soramimi)
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

inline std::string html_encode(std::string_view const &str)
{
	return ::html_encode(str, true);

}

inline std::string html_decode(std::string_view const &str)
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

static inline std::string html_encode(std::string_view const &str, bool utf8through = true)
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

static inline std::string html_decode(std::string_view const &str)
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
	char const *begin_ = nullptr;
	char const *end_ = nullptr;
	char const *ptr_ = nullptr;
	char const *chars_ = nullptr;
	StateType state_ = None;
	bool next_end_element_ = false;
	std::string_view element_name_;
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
public:
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
		std::string_view sv_;
	public:
		EscapedAttributeValue() = default;
		EscapedAttributeValue(std::string_view const &sv)
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
private:
	struct Tag {
		std::string path;
		std::vector<std::pair<std::string_view, std::string_view>> atts;
		EncodedCharacters chars;
		Tag() = default;
		Tag(std::string const &path)
			: path(path)
		{
		}
	};
	std::vector<Tag> stack_;
	std::string last_path_;
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
		stack_.clear();
		stack_.push_back({});
		last_path_.clear();
	}
	void init(char const *begin, char const *end)
	{
		begin_ = begin;
		end_ = end;
		ptr_ = begin_;
		reset_stack();
	}
	std::string const &current_path() const
	{
		if (!last_path_.empty()) return last_path_;
		if (stack_.empty()) return last_path_; // empty string
		return stack_.back().path;
	}
	void pop_path()
	{
		if (!last_path_.empty()) {
			last_path_.clear();
		} else if (!stack_.empty()) {
			stack_.pop_back();
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
		assert(!stack_.empty());
		stack_.back().chars.append(type, begin, end);
	}
	bool is_element_name(std::string_view const &name) const
	{
		return name == element_name_;
	}
	bool is_element_name(char const *name) const
	{
		size_t n = element_name_.size();
		return strncmp(name, element_name_.data(), n) == 0 && name[n] == 0;
	}
	EncodedCharacters const &encoded_chars() const
	{
		assert(!stack_.empty());
		return stack_.back().chars;
	}
public:
	Reader(char const *begin, char const *end)
	{
		init(begin, end);
	}
	
	Reader(char const *ptr, size_t len)
	{
		init(ptr, ptr + len);
	}

	Reader(std::string_view const &s)
	{
		begin_ = s.data();
		end_ = s.data() + s.size();
		init(begin_, end_);
	}
	int depth() const
	{
		return (int)stack_.size();
	}
	struct D {
		std::vector<int> depth_stack;
		bool hold;
	} d;
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
			if (state_ == EndElement && e > 0) {
				e--;
			}
			if (e >= d.depth_stack.back()) {
				return true;
			}
			d.depth_stack.pop_back();
			hold();
		}
		return false;
	}
	bool _internal_next()
	{
		assert(!stack_.empty()); // least one element
		if (state_ == EndElement) {
			size_t i = stack_.size();
			while (i > 1) {
				i--;
				size_t s = stack_[i].path.size();
				size_t n = element_name_.size();
				if (s > n) {
					if (stack_[i].path[s - n - 1] == '/' && memcmp(&stack_[i].path[s - n], element_name_.data(), n) == 0) {
						stack_.resize(i);
						break;
					}
				}
			}
		} else if (state_ == Declaration) {
			if (stack_.size() > 1) {
				stack_.pop_back();
			} else {
				reset_stack();
			}
		}
		while (1) {
			if (!chars_) {
				chars_ = ptr_;
			}
			if (next_end_element_) {
				next_end_element_ = false;
				state_ = EndElement;
				return true;
			}
			if (ptr_ + 9 < end_ && *ptr_ == '<') {
				if (memcmp(ptr_, "<![CDATA[", 9) == 0) {
					ptr_ += 9;
					char const *left = ptr_;
					while (ptr_ + 2 < end_ && memcmp(ptr_, "]]>", 3) != 0) {
						ptr_++;
					}
					append_chars(CharPart::CDATA, left, ptr_);
					ptr_ += 3;
					chars_ = nullptr;
					state_ = Characters;
					return true;
				}
			}
			if (ptr_ < end_ && *ptr_ == '<') {
				ptr_++;
				if (ptr_ + 3 < end_ && memcmp(ptr_, "!--", 3) == 0) {
					ptr_ += 3;
					char const *left = ptr_;
					while (ptr_ + 2 < end_ && memcmp(ptr_, "-->", 3) != 0) {
						ptr_++;
					}
					append_chars(CharPart::Comment, left, ptr_);
					ptr_ += 3;
					chars_ = nullptr;
					state_ = Comment;
					return true;
				}
				char start = 0;
				if (ptr_ < end_ && *ptr_ == '/') {
					start = *ptr_++;
				} else {
					if (ptr_ < end_ && (*ptr_ == '?' || *ptr_ == '!')) {
						start = *ptr_;
					// } else if (ptr_ < end_ && *ptr_ == '!') {
					// 	start = *ptr_++;
					}
				}
				if (ptr_ < end_ && (issymf(*ptr_) || *ptr_ == '?')) {
					char const *left = ptr_++;
					while (ptr_ < end_ && issym(*ptr_)) {
						ptr_++;
					}
					element_name_ = std::string_view(left, ptr_ - left);
					std::vector<std::pair<std::string_view, std::string_view>> atts;
					while (ptr_ < end_ && isspace((unsigned char)*ptr_)) {
						ptr_++;
						while (ptr_ < end_ && isspace((unsigned char)*ptr_)) {
							ptr_++;
						}
						char const *eq = nullptr;
						char quote = 0;
						if (start != '/' && ptr_ < end_ && issymf(*ptr_)) {
							char const *left = ptr_++;
							ptr_++;
							while (ptr_ < end_) {
								if (!eq) {
									if (*ptr_ == '=') {
										eq = ptr_;
										ptr_++;
										if (ptr_ < end_) {
											char q = *ptr_;
											if (q == '\'' || q == '\"') {
												quote = q;
												ptr_++;
											}
										}
									} else if (isspace(*ptr_) || *ptr_ == '>' || *ptr_ == '/') {
										break;
									} else {
										ptr_++;
									}
								} else if (quote != 0 ? (*ptr_ == quote) : (isspace((unsigned char)*ptr_) || *ptr_ == '>' || *ptr_ == '/')) {
									if (*ptr_ == quote) {
										ptr_++;
									}
									std::string_view key;
									std::string_view val;
									if (eq) {
										key = std::string_view(left, eq - left);
										char const *left = eq + 1;
										char const *right = ptr_;
										if (left + 1 < right && left[0] == quote && right[-1] == quote) {
											left++;
											right--;
										}
										val = std::string_view(left, right - left);
									} else {
										key = std::string_view(left, ptr_ - left);
									}
									atts.emplace_back(key, val);
									break;
								} else {
									ptr_++;
								}
							}
						} else {
							break;
						}
					}
					if (ptr_ < end_ && *ptr_ == '/') {
						ptr_++;
						next_end_element_ = true;
					}
					if (start == '!' || (start == '?' && ptr_ < end_ && *ptr_ == '?')) {
						ptr_++;
						if (ptr_ < end_ && *ptr_ == '>') {
							ptr_++;
							stack_.push_back(std::string(element_name_));
							stack_.back().atts = std::move(atts);
							state_ = Declaration;
							return true;
						}
					}
					if (ptr_ < end_ && *ptr_ == '>') {
						ptr_++;
						chars_ = nullptr;
						if (start == '/') {
							state_ = EndElement;
						} else {
							stack_.push_back(current_path() + '/' + std::string(element_name_));
							stack_.back().atts = std::move(atts);
							state_ = StartElement;
						}
						return true;
					}
				}
				state_ = Error;
				return true;
			} else if (ptr_ < end_) {
				if (state_ == EndElement) {
					last_path_ = {};
				}
				while (1) {
					int c = -1;
					if (ptr_ < end_) {
						c = (unsigned char)*ptr_;
					}
					if (c == -1) {
						state_ = None;
						return false;
					}
					if (c == '<') {
						append_chars(CharPart::Text, chars_, ptr_);
						chars_ = nullptr;
						state_ = Characters;
						return true;
					}
					ptr_++;
				}
			} else {
				state_ = None;
				return false;
			}
		}
	}
	StateType state() const
	{
		return state_;
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
	bool is_start_element(std::string_view const &name) const
	{
		return is_start_element() && is_element_name(name);
	}
	bool is_end_element(std::string_view const &name) const
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
		return is_start_element() && match_internal(path);
	}
	bool match_end(char const *path) const
	{
		return is_end_element() && match_internal(path);
	}
	std::string name() const
	{
		return std::string(element_name_);
	}
	bool is_name(char const *s) const
	{
		size_t n = element_name_.size();
		for (size_t i = 0; i < n; i++) {
			if (s[i] != element_name_[i]) return false;
		}
		return s[n] == 0;
	}
	std::string text() const
	{
		return encoded_chars().to_string();
	}
	CharPart characters() const
	{
		assert(!stack_.empty());
		if (stack_.back().chars.chars_.empty()) return {};
		return stack_.back().chars.chars_.back();
	}
	std::optional<EscapedAttributeValue> attribute(std::string_view const &name) const
	{
		assert(!stack_.empty());
		for (auto const &attr : stack_.back().atts) {
			if (attr.first == name) {
				return attr.second;
			}
		}
		return std::nullopt;
	}
	std::string attribute(std::string_view const &name, std::string const &defval) const
	{
		auto s = attribute(name);
		return s ? (std::string)*s : defval;
	}
	std::vector<std::pair<std::string, EscapedAttributeValue>> attributes() const
	{
		std::vector<std::pair<std::string, EscapedAttributeValue>> ret;
		assert(!stack_.empty());
		for (auto const &attr : stack_.back().atts) {
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
	void write_line(std::string_view const &s)
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
	void write_characters(std::string_view const &s)
	{
		close_tag();
		newline_ = false;
		write_line(html_encode(s));
	}
	void write_attribute(std::string_view const &name, std::string_view const &value)
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
