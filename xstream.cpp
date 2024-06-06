
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
		BeginElement,
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
	void init()
	{
		ptr_ = begin_;
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
public:
	xstream(char const *begin, char const *end)
	{
		begin_ = begin;
		end_ = end;
		init();
	}
	
	xstream(std::string_view const &s)
	{
		begin_ = s.data();
		end_ = s.data() + s.size();
		init();
	}

	bool next()
	{
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
			characters_.clear();
			char start = 0;
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
			if (ptr_ < end_ && (*ptr_ == '/' || *ptr_ == '!' || *ptr_ == '?')) {
				start = *ptr_++;
			}
			if (ptr_ < end_ && issymf(*ptr_)) {
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
							} else if (*ptr_ == quote || isspace(*ptr_) || *ptr_ == '>' || *ptr_ == '/') {
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
				if (ptr_ < end_ && *ptr_ == '>') {
					ptr_++;
					chars_ = nullptr;
					state_ = (start == '/') ? EndElement : BeginElement;
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
		 		if (c == '<' || c == -1) {
					characters_.insert(characters_.end(), chars_, ptr_);
					state_ = Characters;
					return true;
				}
				ptr_++;
			}
		}
		state_ = None;
		return false;
	}
	StateType state() const
	{
		return state_;
	}
	std::string text() const
	{
		switch (state()) {
		case BeginElement:
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

int main()
{
	std::string xml = "<hoge><fuga foo='bar'>{<![CDATA[&lt;Hello,&amp; world&gt;]]>|<![CDATA[&lt;Hello,&amp; world&gt;]]>}|<!--comment--></fuga></hoge>";
	
	xstream x(xml);
	while (x.next()) {
		switch (x.state()) {
		case xstream::Characters:
			printf("Characters: %s\n", std::string(x.text()).c_str());
			break;
		case xstream::BeginElement:
			printf("BeginElement: %s\n", std::string(x.text()).c_str());
			{
				auto attrs = x.attributes();
				for (auto const &attr : attrs) {
					printf("  Attribute: %s = %s\n", attr.first.c_str(), attr.second.c_str());
				}
			}
			break;
		case xstream::EndElement:
			printf("EndElement: %s\n", std::string(x.text()).c_str());
			break;
		}
	}

	return 0;
}

