
#include "test.h"
#include <gtest/gtest.h>

//

using namespace xstream;

TEST(XML, XML1)
{
	std::string xml = R"---(<hoge><fuga foo='bar' baz="qux">Hello, world</fuga></hoge>)---";

	xstream::Reader r(xml);
	while (r.next()) {
		if (r.match("/hoge")) {
			EXPECT_EQ(r.name(), "hoge");
		} else if (r.match("/hoge/fuga")) {
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

TEST(XML, XML2)
{
	std::string xml = R"---(<hoge><fuga foo='bar'>Hello, <![CDATA[world]]></fuga></hoge>)---";

	xstream::Reader r(xml);
	while (r.next()) {
		if (r.match("/hoge")) {
			EXPECT_EQ(r.name(), "hoge");
		} else if (r.match("/hoge/fuga")) {
			EXPECT_EQ(r.name(), "fuga");
			EXPECT_EQ(r.attribute("foo", {}), "bar");
		} else if (r.match_end("/hoge/fuga")) {
			EXPECT_EQ(r.attribute("foo", {}), "bar");
			EXPECT_EQ(r.text(), "Hello, world");
		} else if (r.match_end("/hoge")) {
		  EXPECT_EQ(r.name(), "hoge");
		}
	}
}

TEST(XML, XML3)
{
	std::string xml = R"---(<hoge><fuga foo='bar' baz="&lt;&amp;&gt;">He<!--llo, wor-->ld</fuga></hoge>)---";

	xstream::Reader r(xml);
	while (r.next()) {
		if (r.match("/hoge")) {
			EXPECT_EQ(r.name(), "hoge");
		} else if (r.match("/hoge/fuga")) {
			EXPECT_EQ(r.name(), "fuga");
			EXPECT_EQ(r.attribute("foo", {}), "bar");
			EXPECT_EQ(r.attribute("baz", {}), "<&>");
		} else if (r.match_end("/hoge/fuga")) {
			EXPECT_EQ(r.attribute("foo", {}), "bar");
			EXPECT_EQ(r.attribute("baz", {}), "<&>");
			EXPECT_EQ(r.text(), "Held");
		} else if (r.match_end("/hoge")) {
		  EXPECT_EQ(r.name(), "hoge");
		}
	}
}

TEST(XML, XML4)
{
	std::string xml = R"---(<hoge>abc<fuga>def</fuga>ghi</hoge>)---";

	xstream::Reader r(xml);
	while (r.next()) {
		if (r.match("/hoge")) {
			EXPECT_EQ(r.name(), "hoge");
		} else if (r.match("/hoge/fuga")) {
			EXPECT_EQ(r.name(), "fuga");
		} else if (r.match_end("/hoge/fuga")) {
			EXPECT_EQ(r.text(), "def");
		} else if (r.match_end("/hoge")) {
		  EXPECT_EQ(r.name(), "hoge");
		  EXPECT_EQ(r.text(), "abcghi");
		}
	}
}

TEST(XML, XML5)
{
	std::string xml = R"---(<?xml version="1.0"?><hoge><fuga>Hello, world</fuga></hoge>)---";

	xstream::Reader r(xml);
	while (r.next()) {
	  if (r.is_declaration()) {
		EXPECT_EQ(r.name(), "?xml");
		EXPECT_EQ(r.attribute("version", {}), "1.0");
	  } else if (r.match("/hoge")) {
		EXPECT_EQ(r.name(), "hoge");
	  } else if (r.match("/hoge/fuga")) {
		EXPECT_EQ(r.name(), "fuga");
	  } else if (r.match_end("/hoge/fuga")) {
		EXPECT_EQ(r.text(), "Hello, world");
	  } else if (r.match_end("/hoge")) {
		EXPECT_EQ(r.name(), "hoge");
	  }
	}
}

