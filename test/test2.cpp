#include "test.h"
#include <gtest/gtest.h>
#include <memory>

using namespace xstream;

// XMLの名前空間とプレフィックス処理をテスト
TEST(XML, Namespace) 
{
    std::string xml = R"---(
<root xmlns:ns1="http://example.com/ns1" xmlns:ns2="http://example.com/ns2">
    <ns1:element ns1:attr="value1">Content1</ns1:element>
    <ns2:element ns2:attr="value2">Content2</ns2:element>
</root>
)---";

    xstream::Reader r(xml);
    while (r.next()) {
        if (r.match_start("/root")) {
            EXPECT_EQ(r.name(), "root");
            EXPECT_EQ(r.attribute("xmlns:ns1", {}), "http://example.com/ns1");
            EXPECT_EQ(r.attribute("xmlns:ns2", {}), "http://example.com/ns2");
        } else if (r.match_start("/root/ns1:element")) {
            EXPECT_EQ(r.name(), "ns1:element");
            EXPECT_EQ(r.attribute("ns1:attr", {}), "value1");
        } else if (r.match_end("/root/ns1:element")) {
            EXPECT_EQ(r.text(), "Content1");
        } else if (r.match_start("/root/ns2:element")) {
            EXPECT_EQ(r.name(), "ns2:element");
            EXPECT_EQ(r.attribute("ns2:attr", {}), "value2");
        } else if (r.match_end("/root/ns2:element")) {
            EXPECT_EQ(r.text(), "Content2");
        }
    }
}

// 空要素とself-closing tags のテスト
TEST(XML, EmptyElements) 
{
    std::string xml = R"---(
<root>
    <empty></empty>
    <selfclosing />
    <withattrs attr1="value1" attr2="value2" />
</root>
)---";

    xstream::Reader r(xml);
    while (r.next()) {
        if (r.match_start("/root/empty")) {
            EXPECT_EQ(r.name(), "empty");
        } else if (r.match_end("/root/empty")) {
            EXPECT_EQ(r.text(), "");
        } else if (r.match_start("/root/selfclosing")) {
            EXPECT_EQ(r.name(), "selfclosing");
        } else if (r.match_start("/root/withattrs")) {
            EXPECT_EQ(r.name(), "withattrs");
            EXPECT_EQ(r.attribute("attr1", {}), "value1");
            EXPECT_EQ(r.attribute("attr2", {}), "value2");
        }
    }
}

// 複数レベルのネストと複雑な構造のテスト
TEST(XML, NestedStructure) 
{
    std::string xml = R"---(
<library>
    <book category="fiction">
        <title>Neuromancer</title>
        <author>
            <name>William Gibson</name>
            <nationality>American-Canadian</nationality>
        </author>
        <year>1984</year>
        <genres>
            <genre>Cyberpunk</genre>
            <genre>Science Fiction</genre>
        </genres>
    </book>
    <book category="non-fiction">
        <title>A Brief History of Time</title>
        <author>
            <name>Stephen Hawking</name>
            <nationality>British</nationality>
        </author>
        <year>1988</year>
        <genres>
            <genre>Science</genre>
            <genre>Cosmology</genre>
        </genres>
    </book>
</library>
)---";

    // 本のタイトルとジャンルを収集
    struct BookInfo {
        std::string title;
        std::string author;
        std::string category;
        int year;
        std::vector<std::string> genres;
    };
    std::vector<BookInfo> books;
    BookInfo currentBook;
    bool inGenres = false;

    xstream::Reader r(xml);
    while (r.next()) {
        if (r.match_start("/library/book")) {
            currentBook = BookInfo();
            currentBook.category = r.attribute("category", {});
        } else if (r.match_end("/library/book")) {
            books.push_back(currentBook);
        } else if (r.match_end("/library/book/title")) {
            currentBook.title = r.text();
        } else if (r.match_end("/library/book/author/name")) {
            currentBook.author = r.text();
        } else if (r.match_end("/library/book/year")) {
            currentBook.year = std::stoi(r.text());
        } else if (r.match_start("/library/book/genres")) {
            inGenres = true;
        } else if (r.match_end("/library/book/genres")) {
            inGenres = false;
        } else if (r.match_end("/library/book/genres/genre") && inGenres) {
            currentBook.genres.push_back(r.text());
        }
    }

    // 期待される結果の検証
    ASSERT_EQ(books.size(), 2);
    
    EXPECT_EQ(books[0].title, "Neuromancer");
    EXPECT_EQ(books[0].author, "William Gibson");
    EXPECT_EQ(books[0].category, "fiction");
    EXPECT_EQ(books[0].year, 1984);
    ASSERT_EQ(books[0].genres.size(), 2);
    EXPECT_EQ(books[0].genres[0], "Cyberpunk");
    EXPECT_EQ(books[0].genres[1], "Science Fiction");
    
    EXPECT_EQ(books[1].title, "A Brief History of Time");
    EXPECT_EQ(books[1].author, "Stephen Hawking");
    EXPECT_EQ(books[1].category, "non-fiction");
    EXPECT_EQ(books[1].year, 1988);
    ASSERT_EQ(books[1].genres.size(), 2);
    EXPECT_EQ(books[1].genres[0], "Science");
    EXPECT_EQ(books[1].genres[1], "Cosmology");
}

// 特殊文字と処理命令のテスト
TEST(XML, ProcessingInstructions)
{
    std::string xml = R"---(
<?xml version="1.0" encoding="UTF-8"?>
<?xml-stylesheet type="text/xsl" href="style.xsl"?>
<!DOCTYPE html>
<data>
    <item id="1" special="&quot;quoted&quot;">
        Special chars: &lt;&gt;&amp;
    </item>
    <!-- This is a comment -->
    <?custom-instruction param="value"?>
    <item id="2">Regular content</item>
</data>
)---";

    xstream::Reader r(xml);
    int itemCount = 0;

    while (r.next()) {
        if (r.is_declaration() && r.path() == "?xml") {
            EXPECT_EQ(r.name(), "?xml");
            EXPECT_EQ(r.attribute("version", {}), "1.0");
            EXPECT_EQ(r.attribute("encoding", {}), "UTF-8");
        } else if (r.is_declaration() && r.path() == "?xml-stylesheet") {
            EXPECT_EQ(r.name(), "?xml-stylesheet");
            EXPECT_EQ(r.attribute("type", {}), "text/xsl");
            EXPECT_EQ(r.attribute("href", {}), "style.xsl");
        } else if (r.is_declaration() && r.path() == "!DOCTYPE") {
            EXPECT_EQ(r.name(), "!DOCTYPE");
            EXPECT_EQ(r.attribute("html", {}), "");
        } else if (r.match_start("/data/item")) {
            itemCount++;
            if (itemCount == 1) {
                EXPECT_EQ(r.attribute("id", {}), "1");
                EXPECT_EQ(r.attribute("special", {}), "\"quoted\"");
            } else if (itemCount == 2) {
                EXPECT_EQ(r.attribute("id", {}), "2");
            }
        } else if (r.match_end("/data/item")) {
            if (itemCount == 1) {
                // リーディングとトレーリングの空白を削除
                std::string text = r.text();
                text.erase(0, text.find_first_not_of(" \n\r\t"));
                text.erase(text.find_last_not_of(" \n\r\t") + 1);
                EXPECT_EQ(text, "Special chars: <>&");
            } else if (itemCount == 2) {
                EXPECT_EQ(r.text(), "Regular content");
            }
        } else if (r.match_start("/data/item/special")) {
            EXPECT_EQ(r.name(), "special");
        } else if (r.match_start("/data/item/comment")) {
            EXPECT_EQ(r.name(), "comment");
        } else if (r.match_start("/data/item/custom-instruction")) {
            EXPECT_EQ(r.name(), "custom-instruction");
            EXPECT_EQ(r.attribute("param", {}), "value");
        }
    }

    EXPECT_EQ(itemCount, 2);
}

// 再帰的なデータ構造の処理
class MenuItem {
public:
    std::string id;
    std::string label;
    std::vector<std::shared_ptr<MenuItem>> children;

    MenuItem(const std::string& id_, const std::string& label_)
        : id(id_), label(label_) {}

    void addChild(std::shared_ptr<MenuItem> child) {
        children.push_back(child);
    }
};

TEST(XML, RecursiveMenu)
{
    std::string xml = R"---(
<menu>
    <item id="file" label="File">
        <item id="new" label="New" />
        <item id="open" label="Open" />
        <item id="recent" label="Recent Files">
            <item id="doc1" label="Document 1" />
            <item id="doc2" label="Document 2" />
        </item>
        <item id="save" label="Save" />
        <item id="exit" label="Exit" />
    </item>
    <item id="edit" label="Edit">
        <item id="cut" label="Cut" />
        <item id="copy" label="Copy" />
        <item id="paste" label="Paste" />
    </item>
    <item id="help" label="Help">
        <item id="about" label="About" />
    </item>
</menu>
)---";

    std::vector<std::shared_ptr<MenuItem>> menuItems;

    xstream::Reader r(xml);
    while (r.next()) {
        if (r.match_start("/menu/item")) {
            auto parseMenu = [&](auto self, std::shared_ptr<MenuItem> parent) -> void {
                r.nest();
                do {
                    if (r.is_start_element() && r.is_name("item")) {
                        std::string id = r.attribute("id", {});
                        std::string label = r.attribute("label", {});
                        
                        auto menuItem = std::make_shared<MenuItem>(id, label);
                        if (parent) {
                            parent->addChild(menuItem);
                        } else {
                            menuItems.push_back(menuItem);
                        }
                        
                        if (!r.next()) break;
                        self(self, menuItem);
                    }
                } while (r.next());
            };
            
            std::string id = r.attribute("id", {});
            std::string label = r.attribute("label", {});
            auto rootItem = std::make_shared<MenuItem>(id, label);
            menuItems.push_back(rootItem);
            
            if (!r.next()) break;
            parseMenu(parseMenu, rootItem);
        }
    }

    // 検証
    ASSERT_EQ(menuItems.size(), 3);
    
    // File メニュー
    EXPECT_EQ(menuItems[0]->id, "file");
    EXPECT_EQ(menuItems[0]->label, "File");
    ASSERT_EQ(menuItems[0]->children.size(), 5);
    EXPECT_EQ(menuItems[0]->children[0]->id, "new");
    EXPECT_EQ(menuItems[0]->children[1]->id, "open");
    EXPECT_EQ(menuItems[0]->children[2]->id, "recent");
    EXPECT_EQ(menuItems[0]->children[3]->id, "save");
    EXPECT_EQ(menuItems[0]->children[4]->id, "exit");
    
    // Recent Files サブメニュー
    ASSERT_EQ(menuItems[0]->children[2]->children.size(), 2);
    EXPECT_EQ(menuItems[0]->children[2]->children[0]->id, "doc1");
    EXPECT_EQ(menuItems[0]->children[2]->children[1]->id, "doc2");
    
    // Edit メニュー
    EXPECT_EQ(menuItems[1]->id, "edit");
    EXPECT_EQ(menuItems[1]->label, "Edit");
    ASSERT_EQ(menuItems[1]->children.size(), 3);
    EXPECT_EQ(menuItems[1]->children[0]->id, "cut");
    EXPECT_EQ(menuItems[1]->children[1]->id, "copy");
    EXPECT_EQ(menuItems[1]->children[2]->id, "paste");
    
    // Help メニュー
    EXPECT_EQ(menuItems[2]->id, "help");
    EXPECT_EQ(menuItems[2]->label, "Help");
    ASSERT_EQ(menuItems[2]->children.size(), 1);
    EXPECT_EQ(menuItems[2]->children[0]->id, "about");
}

