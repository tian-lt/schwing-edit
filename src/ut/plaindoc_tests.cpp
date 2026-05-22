// std
#include <string>

// gtest
#include <gtest/gtest.h>

// schwing
#include "plaindoc.hpp"

namespace swg::ut::plaindoc_ut {

namespace {
class test_host : public host {
 public:
  explicit test_host(eol eol = eol::crlf) : doc_(this, "", 12.0, eol) { doc = &doc_; }

  void on_invalidate(rect) override {}

  std::string text() const { return doc_.get(0, doc_.length()); }
  size_t caret() const { return caret_pos(); }

 private:
  plaindoc doc_;
};
}  // namespace

TEST(plaindoc_tests, backspace_treats_crlf_as_one_character) {
  test_host host{eol::crlf};
  host.insert_char("a");
  host.linefeed();
  ASSERT_EQ(host.text(), "a\r\n");
  ASSERT_EQ(host.caret(), 3u);

  host.erase_char();

  EXPECT_EQ(host.text(), "a");
  EXPECT_EQ(host.caret(), 1u);
}

TEST(plaindoc_tests, delete_treats_crlf_as_one_character) {
  test_host host{eol::crlf};
  host.insert_char("a");
  host.linefeed();
  host.insert_char("b");
  ASSERT_EQ(host.text(), "a\r\nb");
  host.caret_pos(1);

  host.delete_char();

  EXPECT_EQ(host.text(), "ab");
  EXPECT_EQ(host.caret(), 1u);
}

TEST(plaindoc_tests, delete_removes_utf8_character) {
  test_host host{eol::lf};
  host.insert_char("a");
  host.insert_char(reinterpret_cast<const char*>(u8"\u00e9"));
  ASSERT_EQ(host.text(), reinterpret_cast<const char*>(u8"a\u00e9"));

  host.erase_char();
  ASSERT_EQ(host.text(), "a");
  ASSERT_EQ(host.caret(), 1u);
}

}  // namespace swg::ut::plaindoc_ut
