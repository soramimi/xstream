#ifndef TEST_H
#define TEST_H

#include <xstream.h>
#include <gtest/gtest.h>
#include <initializer_list>

namespace test_support {

class EventTrace {
	bool include_text_;
	std::vector<std::string> events_;
public:
	explicit EventTrace(bool include_text = false) : include_text_(include_text) {}
	bool next(xstream::Reader *reader)
	{
		if (!reader->next()) return false;
		EXPECT_EQ(reader->match_start(reader->path().c_str()),
			reader->state() == xstream::Reader::StartElement || reader->state() == xstream::Reader::Declaration);
		EXPECT_EQ(reader->match_end(reader->path().c_str()), reader->state() == xstream::Reader::EndElement);
		char const *kind = nullptr;
		switch (reader->state()) {
		case xstream::Reader::StartElement: kind = "start"; break;
		case xstream::Reader::EndElement: kind = "end"; break;
		case xstream::Reader::Declaration: kind = "declaration"; break;
		case xstream::Reader::Comment: kind = "comment"; break;
		case xstream::Reader::Characters:
			if (!include_text_) return true;
			kind = "text";
			break;
		default: ADD_FAILURE() << "next() returned an invalid event state"; return false;
		}
		events_.push_back(std::string(kind) + " " + reader->path());
		return true;
	}
	void expect(xstream::Reader const &reader, std::initializer_list<std::string> expected) const
	{
		EXPECT_EQ(events_, std::vector<std::string>(expected));
		EXPECT_EQ(reader.result(), xstream::Reader::ReadResult::EndDocument)
			<< reader.error().message << " at byte " << reader.error().offset;
		EXPECT_EQ(reader.error().code, xstream::Reader::ErrorCode::None);
	}
};

} // namespace test_support

#endif // TEST_H
