
#include "xstream.h"
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#include <sys/types.h>

int main()
{
#if 0
	std::string xml = "<hoge><fuga foo='bar'>Hello, <![CDATA[world]]></fuga></hoge>";
#else
	char const *path = "Z:/_tmp/test.xml";
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

	xstream x(xml);
	while (x.next()) {
		switch (x.state()) {
		case xstream::Characters:
			fprintf(stderr, "Characters: %s\n", std::string(x.text()).c_str());
			break;
		case xstream::BeginElement:
			fprintf(stderr, "BeginElement: %s\n", std::string(x.text()).c_str());
			{
				auto attrs = x.attributes();
				for (auto const &attr : attrs) {
					fprintf(stderr, "  Attribute: %s = %s\n", attr.first.c_str(), attr.second.c_str());
				}
			}
			break;
		case xstream::EndElement:
			fprintf(stderr, "EndElement: %s\n", std::string(x.text()).c_str());
			break;
		}
	}
	fprintf(stderr, "done\n");
	return 0;
}

