
#include "xstream.h"

#ifdef _WIN32	
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#include <sys/types.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

void test1()
{
#if 1
	std::string xml = R"---(<hoge><fuga foo='bar'>Hello, <![CDATA[world]]></fuga></hoge>)---";
#else
	
#ifdef _WIN32	
	char const *path = "Z:/_tmp/test.xml";
#else
	char const *path = "/tmp/test.xml";
#endif
	std::vector<char> data;
	int fd = open(path, O_RDONLY);
	if (fd != -1) {
		struct stat st;
		if (fstat(fd, &st) == 0) {
			data.resize(st.st_size);
			read(fd, data.data(), data.size());
		}
	}
	if (data.empty()) return 1;

	std::string_view xml(data.data(), data.size());

#endif

	xstream::Reader x(xml);
	while (x.next()) {
		if (x.is_characters()) {
			auto v = x.characters().decode();
			std::string_view sv(v.data(), v.size());
			printf("Characters: %s\n", std::string(sv).c_str());
		} else if (x.is_start_element()) {
			printf("StartElement: %s\n", x.name().c_str());
		} else if (x.is_end_element()) {
			printf("EndElement: %s @ %s\n", x.name().c_str(), x.path().c_str());
		}
	}
	printf("done\n");
}

void test2()
{
	auto EXPECT_EQ = [](auto a, auto b) {
		if (a != b) {
			printf("EXPECT_EQ failed: %s != %s\n", std::string(a).c_str(), std::string(b).c_str());
		}
	};
	
	std::string_view xml = R"---(<hoge><fuga foo='bar' baz="qux"><![CDATA[Hello, world]]></fuga></hoge>)---";
	size_t pos = 0;
	
	xstream::Reader r;
	r.parse([&](){
		if (pos < xml.size()) {
			size_t n = 1;
			r.input(std::string_view(xml.data() + pos, n));
			pos += n;
		}
	});
	
	while (r.next()) {
		if (r.match_start("/hoge")) {
			EXPECT_EQ(r.name(), "hoge");
		} else if (r.match_start("/hoge/fuga")) {
			EXPECT_EQ(r.name(), "fuga");
			EXPECT_EQ(r.attribute("foo", {}), "bar");
			EXPECT_EQ(r.attribute("baz", {}), "qux");
		} else if (r.match_end("/hoge/fuga")) {
			EXPECT_EQ(r.attribute("foo", {}), "bar");
			EXPECT_EQ(r.attribute("baz", {}), "qux");
			EXPECT_EQ(r.text(), "Hello, world");
		} else if (r.match_end("/hoge")) {
			EXPECT_EQ(r.name(), "hoge");
		}
	}
}

int main()
{
	test2();
	return 0;
}
