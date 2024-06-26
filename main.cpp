
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

int main()
{
#if 0
	std::string xml = "<hoge><fuga foo='bar'>Hello, <![CDATA[world]]></fuga></hoge>";
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

	xstream x(xml);
	while (x.next()) {
		if (x.isCharacters()) {
			printf("Characters: %s\n", std::string(x.text()).c_str());
		} else if (x.isStartElement()) {
			printf("StartElement: %s\n", std::string(x.text()).c_str());
		} else if (x.isEndElement()) {
			printf("EndElement: %s @ %s\n", std::string(x.text()).c_str(), x.path().c_str());
		}
	}
	printf("done\n");
	return 0;
}

