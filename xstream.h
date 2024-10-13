
#ifndef XSTREAM_H
#define XSTREAM_H

#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>
#include <functional>
#include "urlencode.h"
#include "htmlencode.h"

namespace xstream {

class reader {
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
		Type type = Text;
		std::string_view sv;
		CharPart() = default;
		CharPart(Type type, char const *begin, char const *end)
			: type(type)
			, sv(begin, end - begin)
		{
		}
	};
	std::vector<CharPart> characters_;
	std::vector<std::pair<std::string_view, std::string_view>> attributes_;
	std::vector<std::string> paths_;
	std::string current_path_;
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
	std::string decode_html_string(std::vector<CharPart> const &parts) const
	{
		std::vector<char> v;
		size_t len = 0;
		for (auto &part : parts) {
			len += part.sv.size();
		}
		v.reserve(len + 100);
		for (auto &part : parts) {
			switch (part.type) {
			case CharPart::Text:
				{
					std::string s = html_decode(part.sv);
					v.insert(v.end(), s.begin(), s.end());
				}
				break;
			case CharPart::CDATA:
				v.insert(v.end(), part.sv.begin(), part.sv.end());
				break;
			case CharPart::Comment:
				break;
			}
		}
		return std::string(v.data(), v.size());
	}
	void init(char const *begin, char const *end)
	{
		begin_ = begin;
		end_ = end;
		ptr_ = begin_;
		paths_.clear();
		current_path_ = "/";
	}
	bool match_internal(char const *path) const
	{
		int n = current_path_.size();
		if (strncmp(path, current_path_.c_str(), n) == 0) {
			if (path[n] == 0) {
				return true;
			}
			if (path[n] == '/' && path[n + 1] == 0) {
				return true;
			}
		}
		return false;
	}
	void insert_chars(CharPart::Type type, char const *begin, char const *end)
	{
		if (begin != end) {
			characters_.emplace_back(type, begin, end);
		}
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
public:
	reader(char const *begin, char const *end)
	{
		init(begin, end);
	}
	
	reader(char const *ptr, size_t len)
	{
		init(ptr, ptr + len);
	}

	reader(std::string_view const &s)
	{
		begin_ = s.data();
		end_ = s.data() + s.size();
		init(begin_, end_);
	}

	bool next()
	{
		if (state_ == EndElement) {
			if (paths_.empty()) {
				current_path_ = "/";
			} else {
				current_path_ = paths_.back();
				paths_.pop_back();
			}
		}
		while (1) {
			if (!chars_) {
				characters_.clear();
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
					insert_chars(CharPart::CDATA, left, ptr_);
					ptr_ += 3;
					chars_ = nullptr;
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
					insert_chars(CharPart::Text, left, ptr_);
					ptr_ += 3;
					state_ = Comment;
					return true;
				}
				char start = 0;
				characters_.clear();
				if (ptr_ < end_ && *ptr_ == '?') {
					start = *ptr_;
				} else if (ptr_ < end_ && (*ptr_ == '/' || *ptr_ == '!')) {
					start = *ptr_++;
				}
				if (ptr_ < end_ && (issymf(*ptr_) || *ptr_ == '?')) {
					char const *left = ptr_++;
					while (ptr_ < end_ && issym(*ptr_)) {
						ptr_++;
					}
					characters_ = {};
					element_name_ = std::string_view(left, ptr_ - left);
					attributes_.clear();
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
									attributes_.emplace_back(key, val);
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
					if (start == '?' && ptr_ < end_ && *ptr_ == '?') {
						ptr_++;
					}
					if (ptr_ < end_ && *ptr_ == '>') {
						ptr_++;
						chars_ = nullptr;
						if (start == '/') {
							state_ = EndElement;
						} else {
							if (start == '?') {
								current_path_ = '/' + std::string(element_name_);
							} else if (paths_.empty()) {
								paths_.push_back("/");
								current_path_ = '/' + std::string(element_name_);
							} else {
								paths_.push_back(current_path_);
								current_path_ = current_path_  + '/' + std::string(element_name_);
							}
							state_ = StartElement;
						}
						return true;
					}
				}
				state_ = Error;
				return true;
			} else if (ptr_ < end_) {
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
						insert_chars(CharPart::Text, chars_, ptr_);
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
	std::string path() const
	{
		return current_path_;
	}
	bool match(char const *path) const
	{
		return is_start_element() && match_internal(path);
	}
	bool match_end(char const *path) const
	{
		return is_end_element() && match_internal(path);
	}
	std::string text() const
	{
		switch (state()) {
		case StartElement:
		case EndElement:
			return std::string(element_name_);
		case Characters:
			return decode_html_string(characters_);
		}
		return {};
	}
	std::string attribute(std::string_view const &name, std::string const &defval = {}) const
	{
		for (auto const &attr : attributes_) {
			if (attr.first == name) {
				return html_decode(attr.second);
			}
		}
		return defval;
	}
	std::vector<std::pair<std::string, std::string>> attributes() const
	{
		std::vector<std::pair<std::string, std::string>> ret;
		for (auto const &attr : attributes_) {
			ret.emplace_back(std::string(attr.first), html_decode(attr.second));
		}
		return ret;
	}
}; // class reader

class writer {
private:
	std::function<int (char const *p, int n)> fn_writer;
	std::vector<char> line_;
	bool inside_tag_ = false;
	size_t newline_ = false;
	std::vector<std::string> element_stack_;
	void write_line(std::string_view const &s)
	{
		line_.insert(line_.end(), s.begin(), s.end());
	}
	void write_indent(size_t n)
	{
		n *= 2;
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
	writer(std::function<int (char const *p, int n)> fn_writer)
		: fn_writer(fn_writer)
	{
	}
	void start_document()
	{
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
}; // class writer

} // namespace xstream
#endif // XSTREAM_H
