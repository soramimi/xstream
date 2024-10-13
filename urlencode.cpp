
#include "urlencode.h"
#include <cstdlib>
#include <cstdio>
#include <cctype>
#include <cstring>
#include <vector>


#ifdef WIN32
#pragma warning(disable:4996)
#endif

namespace {

inline void vecprint(std::vector<char> *out, char c)
{
	out->push_back(c);
}

inline void vecprint(std::vector<char> *out, char const *s)
{
	out->insert(out->end(), s, s + strlen(s));
}

inline std::string_view to_string(std::vector<char> const &vec)
{
	if (!vec.empty()) {
		return {vec.data(), vec.size()};
	}
	return {};
}

} // namespace

static void url_encode_(char const *ptr, char const *end, std::vector<char> *out, bool encodeslash, bool utf8through)
{
	while (ptr < end) {
		int c = (unsigned char)*ptr;
		ptr++;
		if (isalnum(c) || strchr("_.-~", c)) {
			vecprint(out, c);
		} else if (utf8through && c >= 0x80) {
			vecprint(out, c);
		} else if (c == '/' && !encodeslash) {
			vecprint(out, c);
		} else if (c == ' ') {
			vecprint(out, '+');
		} else {
			char tmp[10];
			sprintf(tmp, "%%%02X", c);
			vecprint(out, tmp[0]);
			vecprint(out, tmp[1]);
			vecprint(out, tmp[2]);
		}
	}
}

std::string url_encode(char const *str, char const *end, bool encodeslash, bool utf8through)
{
	if (!str) {
		return std::string();
	}

	std::vector<char> out;
	out.reserve(end - str + 10);

	url_encode_(str, end, &out, encodeslash, utf8through);

	return (std::string)to_string(out);
}

std::string url_encode(char const *str, size_t len, bool encodeslash, bool utf8through)
{
	return url_encode(str, str + len, encodeslash, utf8through);
}

std::string url_encode(char const *str, bool utf8through)
{
	return url_encode(str, strlen(str), utf8through);
}

std::string url_encode(std::string_view const &str, bool encodeslash, bool utf8through)
{
	char const *begin = str.data();
	char const *end = begin + str.size();
	char const *ptr = begin;

	while (ptr < end) {
		int c = (unsigned char)*ptr;
		if (isalnum(c) || strchr("_.-~", c)) {
			// thru
		} else if (c == '/' && !encodeslash) {
			// thru
		} else {
			break;
		}
		ptr++;
	}
	if (ptr == end) {
		return std::string(str);
	}

	std::vector<char> out;
	out.reserve(str.size() + 10);

	out.insert(out.end(), begin, ptr);
	url_encode_(ptr, end, &out, encodeslash, utf8through);

	return (std::string)to_string(out);
}

static void url_decode_(char const *ptr, char const *end, std::vector<char> *out)
{
	while (ptr < end) {
		int c = (unsigned char)*ptr;
		ptr++;
		if (c == '+') {
			c = ' ';
		} else if (c == '%' && isxdigit((unsigned char)ptr[0]) && isxdigit((unsigned char)ptr[1])) {
			char tmp[3]; // '%XX'
			tmp[0] = ptr[0];
			tmp[1] = ptr[1];
			tmp[2] = 0;
			c = strtol(tmp, nullptr, 16);
			ptr += 2;
		}
		vecprint(out, c);
	}
}

std::string url_decode(char const *str, char const *end)
{
	if (!str) {
		return std::string();
	}

	std::vector<char> out;
	out.reserve(end - str + 10);

	url_decode_(str, end, &out);

	return (std::string)to_string(out);
}

std::string url_decode(char const *str, size_t len)
{
	return url_decode(str, str + len);
}

std::string url_decode(char const *str)
{
	return url_decode(str, strlen(str));
}

std::string url_decode(std::string_view const &str)
{
	char const *begin = str.data();
	char const *end = begin + str.size();
	char const *ptr = begin;

	while (ptr < end) {
		int c = *ptr & 0xff;
		if (c == '+' || c == '%') {
			break;
		}
		ptr++;
	}
	if (ptr == end) {
		return std::string(str);
	}

	std::vector<char> out;
	out.reserve(str.size() + 10);

	out.insert(out.end(), begin, ptr);
	url_decode_(ptr, end, &out);

	return (std::string)to_string(out);
}
