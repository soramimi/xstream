
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
			printf("Characters: %s\n", std::string(x.text()).c_str());
		} else if (x.is_start_element()) {
			printf("StartElement: %s\n", std::string(x.text()).c_str());
		} else if (x.is_end_element()) {
			printf("EndElement: %s @ %s\n", std::string(x.text()).c_str(), x.path().c_str());
		}
	}
	printf("done\n");
}

void test2()
{
	std::string xml = R"---(<hoge>abc<fuga foo='bar'>def</fuga>ghi</hoge>)---";
	xstream::Reader r(xml);
	while (r.next()) {
		if (r.match("/hoge")) {
		} else if (r.is_end_element()) {
			printf("%s: %s\n", r.path().c_str(), r.characters().to_string().c_str());
			for (auto const &a : r.attributes()) {
				printf("%s: %s\n", a.first.c_str(), a.second.c_str());
			}
		}
	}
}

int main()
{
	test2();
	return 0;
}
