
#include "test.h"
#include <gtest/gtest.h>
#include <memory>

//

using namespace xstream;

TEST(XML, XML1)
{
	std::string xml = R"---(<hoge><fuga foo='bar' baz="qux">Hello, world</fuga></hoge>)---";

	xstream::Reader r(xml);
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

TEST(XML, XML2)
{
	std::string xml = R"---(<hoge><fuga foo='bar'>Hello, <![CDATA[world]]></fuga></hoge>)---";

	xstream::Reader r(xml);
	while (r.next()) {
		if (r.match_start("/hoge")) {
			EXPECT_EQ(r.name(), "hoge");
		} else if (r.match_start("/hoge/fuga")) {
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
		if (r.match_start("/hoge")) {
			EXPECT_EQ(r.name(), "hoge");
		} else if (r.match_start("/hoge/fuga")) {
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
		if (r.match_start("/hoge")) {
			EXPECT_EQ(r.name(), "hoge");
		} else if (r.match_start("/hoge/fuga")) {
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
	  } else if (r.match_start("/hoge")) {
		EXPECT_EQ(r.name(), "hoge");
	  } else if (r.match_start("/hoge/fuga")) {
		EXPECT_EQ(r.name(), "fuga");
	  } else if (r.match_end("/hoge/fuga")) {
		EXPECT_EQ(r.text(), "Hello, world");
	  } else if (r.match_end("/hoge")) {
		EXPECT_EQ(r.name(), "hoge");
	  }
	}
}

class Node {
public:
	enum Type {
		None,
		Root,
		Drive,
		Dir,
		File,
	};
private:
	Type type_ = None;
	std::string name_;
	int64_t size_ = 0;
	std::vector<std::shared_ptr<Node>> children_;
public:
	Node() = default;
	Node(Type type, const std::string &name, int64_t size = 0)
		: type_(type)
		, name_(name)
		, size_(size)
	{
	}
	Type type() const
	{
		return type_;
	}
	std::string name() const
	{
		return name_;
	}
	size_t childs() const
	{
		return childs();
	}
	Node & operator[](size_t i)
	{
		return *children_[i];
	}
	Node *add_child(Node const &child)
	{
		children_.push_back(std::make_shared<Node>(child));
		return children_.back().get();
	}
	int64_t size() const
	{
		return size_;
	}
};

TEST(XML, XML6)
{
	std::string xml = R"---(
<?xml version="1.0" encoding="UTF-8"?>
<tree>
  <drive name="C">
	<dir name="Program Files">
	  <dir name="Common Files">
		<file name="shared.dll" size="20480" />
	  </dir>
	  <file name="app.exe" size="102400" />
	</dir>
	<dir name="Users">
	  <dir name="Alice">
		<file name="document.txt" size="4096" />
		<file name="photo.jpg" size="512000" />
	  </dir>
	</dir>
  </drive>
  <drive name="D">
	<dir name="Games">
	  <dir name="RPG">
		<dir name="Saves">
		  <file name="save1.dat" size="8192" />
		  <file name="save2.dat" size="8192" />
		</dir>
	  </dir>
	  <file name="game.exe" size="204800" />
	</dir>
	<file name="readme.txt" size="1024" />
  </drive>
  <drive name="E">
	<dir name="Music">
	  <file name="song1.mp3" size="4096000" />
	  <file name="song2.mp3" size="5120000" />
	</dir>
	<dir name="Videos">
	  <dir name="Movies">
		<file name="movie.mp4" size="1048576000" />
	  </dir>
	</dir>
  </drive>
</tree>
)---";

	Node tree(Node::Root, "root");

	xstream::Reader r(xml);
	while (r.next()) {
		if (r.match_start("/tree/drive")) {
			auto ParseTree = [&](auto self, Node *parent)->void{
				r.nest();
				do {
					if (r.is_start_element()) {
						Node::Type type = Node::None;
						if (r.is_name("file")) {
							type = Node::File;
						} else if (r.is_name("dir")) {
							type = Node::Dir;
						} else if (r.is_name("drive")) {
							type = Node::Drive;
						} else {
							continue;
						}
						std::string name = r.attribute("name", {});
						int64_t size = std::strtoll(r.attribute("size", "0").c_str(), nullptr, 10);
						auto *p = parent->add_child({type, name, size});
						if (type == Node::Dir || type == Node::Drive) {
							if (!r.next()) break;
							self(self, p);
						}
					}
				} while (r.next());
			};
			ParseTree(ParseTree, &tree);
		}
	}

	ASSERT_EQ(tree.childs(), 3);
	EXPECT_EQ(tree[0].type(), Node::Drive);
	EXPECT_EQ(tree[0].name(), "C");
	ASSERT_EQ(tree[0].childs(), 2);
	EXPECT_EQ(tree[0][0].type(), Node::Dir);
	EXPECT_EQ(tree[0][0].name(), "Program Files");
	ASSERT_EQ(tree[0][0].childs(), 2);
	EXPECT_EQ(tree[0][0][0].type(), Node::Dir);
	EXPECT_EQ(tree[0][0][0].name(), "Common Files");
	EXPECT_EQ(tree[0][0][1].type(), Node::File);
	EXPECT_EQ(tree[0][0][1].name(), "app.exe");
	EXPECT_EQ(tree[0][0][1].size(), 102400);
	EXPECT_EQ(tree[0][1].type(), Node::Dir);
	EXPECT_EQ(tree[0][1].name(), "Users");
	ASSERT_EQ(tree[0][1].childs(), 1);
	EXPECT_EQ(tree[0][1][0].type(), Node::Dir);
	EXPECT_EQ(tree[0][1][0].name(), "Alice");
	EXPECT_EQ(tree[0][1][0].childs(), 2);
	EXPECT_EQ(tree[0][1][0][0].type(), Node::File);
	EXPECT_EQ(tree[0][1][0][0].name(), "document.txt");
	EXPECT_EQ(tree[0][1][0][0].size(), 4096);
	EXPECT_EQ(tree[0][1][0][1].type(), Node::File);
	EXPECT_EQ(tree[0][1][0][1].name(), "photo.jpg");
	EXPECT_EQ(tree[0][1][0][1].size(), 512000);
	EXPECT_EQ(tree[1].type(), Node::Drive);
	EXPECT_EQ(tree[1].name(), "D");
	ASSERT_EQ(tree[1].childs(), 2);
	EXPECT_EQ(tree[1][0].type(), Node::Dir);
	EXPECT_EQ(tree[1][0].name(), "Games");
	ASSERT_EQ(tree[1][0].childs(), 2);
	EXPECT_EQ(tree[1][0][0].type(), Node::Dir);
	EXPECT_EQ(tree[1][0][0].name(), "RPG");
	ASSERT_EQ(tree[1][0][0].childs(), 1);
	EXPECT_EQ(tree[1][0][0][0].type(), Node::Dir);
	EXPECT_EQ(tree[1][0][0][0].name(), "Saves");
	ASSERT_EQ(tree[1][0][0][0].childs(), 2);
	EXPECT_EQ(tree[1][0][0][0][0].type(), Node::File);
	EXPECT_EQ(tree[1][0][0][0][0].name(), "save1.dat");
	EXPECT_EQ(tree[1][0][0][0][0].size(), 8192);
	EXPECT_EQ(tree[1][0][0][0][1].type(), Node::File);
	EXPECT_EQ(tree[1][0][0][0][1].name(), "save2.dat");
	EXPECT_EQ(tree[1][0][0][0][1].size(), 8192);
	EXPECT_EQ(tree[1][0][1].type(), Node::File);
	EXPECT_EQ(tree[1][0][1].name(), "game.exe");
	EXPECT_EQ(tree[1][0][1].size(), 204800);
	EXPECT_EQ(tree[1][1].type(), Node::File);
	EXPECT_EQ(tree[1][1].name(), "readme.txt");
	EXPECT_EQ(tree[1][1].size(), 1024);
	EXPECT_EQ(tree[2].type(), Node::Drive);
	EXPECT_EQ(tree[2].name(), "E");
	ASSERT_EQ(tree[2].childs(), 2);
	EXPECT_EQ(tree[2][0].type(), Node::Dir);
	EXPECT_EQ(tree[2][0].name(), "Music");
	ASSERT_EQ(tree[2][0].childs(), 2);
	EXPECT_EQ(tree[2][0][0].type(), Node::File);
	EXPECT_EQ(tree[2][0][0].name(), "song1.mp3");
	EXPECT_EQ(tree[2][0][0].size(), 4096000);
	EXPECT_EQ(tree[2][0][1].type(), Node::File);
	EXPECT_EQ(tree[2][0][1].name(), "song2.mp3");
	EXPECT_EQ(tree[2][0][1].size(), 5120000);
	EXPECT_EQ(tree[2][1].type(), Node::Dir);
	EXPECT_EQ(tree[2][1].name(), "Videos");
	ASSERT_EQ(tree[2][1].childs(), 1);
	EXPECT_EQ(tree[2][1][0].type(), Node::Dir);
	EXPECT_EQ(tree[2][1][0].name(), "Movies");
	ASSERT_EQ(tree[2][1][0].childs(), 1);
	EXPECT_EQ(tree[2][1][0][0].type(), Node::File);
	EXPECT_EQ(tree[2][1][0][0].name(), "movie.mp4");
	EXPECT_EQ(tree[2][1][0][0].size(), 1048576000);
}




