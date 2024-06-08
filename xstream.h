
#ifndef XSTREAM_H
#define XSTREAM_H

#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

class xstream {
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
	std::vector<char> characters_;
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
	std::string decode_string(std::string_view const &s) const
	{
		std::vector<char> vec;
		vec.reserve(1024);
		char const *begin = s.data();
		char const *end = s.data() + s.size();
		char const *ptr = begin;
		while (ptr < end) {
			if (*ptr == '&') {
				ptr++;
				size_t n = 0;
				for (n = 0; ptr + n < end && ptr[n] != ';'; n++);
				auto IsEntity = [&](char const *name) {
					size_t i = 0;
					while (1) {
						if (name[i] == 0) return i == n;
						if (ptr[i] != name[i]) return false;
						i++;
					}
				};
				if (IsEntity("amp")) {
					vec.push_back('&');
				} else if (IsEntity("lt")) {
					vec.push_back('<');
				} else if (IsEntity("gt")) {
					vec.push_back('>');
				} else if (IsEntity("quot")) {
					vec.push_back('\"');
				} else if (IsEntity("apos")) {
					vec.push_back('\'');
				}
				ptr += n + 1;
			} else {
				vec.push_back(*ptr);
				ptr++;
			}
		}
		return std::string(vec.data(), vec.size());
	}
	std::string decode_string(std::vector<char> const &s) const
	{
		return decode_string(std::string_view(s.data(), s.size()));
	}
	void init(char const *begin, char const *end)
	{
		begin_ = begin;
		end_ = end;
		ptr_ = begin_;
		paths_.clear();
		current_path_ = "/";
	}
public:
	xstream(char const *begin, char const *end)
	{
		init(begin, end);
	}
	
	xstream(std::string_view const &s)
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
			if (ptr_ < end_ && *ptr_ == '<') {
				ptr_++;
				if (ptr_ + 3 < end_ && memcmp(ptr_, "!--", 3) == 0) {
					ptr_ += 3;
					char const *left = ptr_;
					while (ptr_ + 2 < end_ && memcmp(ptr_, "-->", 3) != 0) {
						ptr_++;
					}
					characters_.insert(characters_.end(), left, ptr_);
					ptr_ += 3;
					state_ = Comment;
					return true;
				}
				if (ptr_ + 8 < end_ && memcmp(ptr_, "![CDATA[", 8) == 0) {
					ptr_ += 8;
					char const *left = ptr_;
					while (ptr_ + 2 < end_ && memcmp(ptr_, "]]>", 3) != 0) {
						ptr_++;
					}
					characters_.insert(characters_.end(), left, ptr_);
					ptr_ += 3;
					chars_ = nullptr;
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
						characters_.insert(characters_.end(), chars_, ptr_);
						if (ptr_ + 1 < end_ && (ptr_[1] == '/' || ptr_[1] == '!')) {
							if (state_ == StartElement || state_ == Characters) {
								chars_ = nullptr;
								state_ = Characters;
								return true;
							}
						}
						break;
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
	std::string path() const
	{
		return current_path_;
	}
	bool match(char const *path) const
	{
		if (strncmp(path, current_path_.c_str(), current_path_.size()) == 0) {
			if (path[current_path_.size()] == 0) {
				return true;
			}
		}
		return false;
	}
	std::string text() const
	{
		switch (state()) {
		case StartElement:
		case EndElement:
			return std::string(element_name_);
		case Characters:
			return decode_string(characters_);
		}
		return {};
	}
	std::string attribute(std::string_view const &name, std::string const &defval = {}) const
	{
		for (auto const &attr : attributes_) {
			if (attr.first == name) {
				return decode_string(attr.second);
			}
		}
		return defval;
	}
	std::vector<std::pair<std::string, std::string>> attributes() const
	{
		std::vector<std::pair<std::string, std::string>> ret;
		for (auto const &attr : attributes_) {
			ret.emplace_back(std::string(attr.first), decode_string(attr.second));
		}
		return ret;
	}
};

#endif // XSTREAM_H
