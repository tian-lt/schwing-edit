// std
#include <string>

// gtest
#include <gtest/gtest.h>

// schwing
#include "linetable.hpp"
#include "plaindoc.hpp"

namespace swg::ut::plaindoc_ut {

TEST(plaindoc_tests, empty_doc_is_empty) {
  plaindoc pd{12.0, eol::lf};
  EXPECT_EQ(pd.length(), 0u);
  EXPECT_EQ(pd.get(0, 0), "");
}

TEST(plaindoc_tests, insert_at_start) {
  plaindoc pd{12.0, eol::lf};
  pd.insert(0, "hello");
  EXPECT_EQ(pd.length(), 5u);
  EXPECT_EQ(pd.get(0, pd.length()), "hello");
}

TEST(plaindoc_tests, insert_appends) {
  plaindoc pd{12.0, eol::lf};
  pd.insert(0, "hello");
  pd.insert(5, " world");
  EXPECT_EQ(pd.length(), 11u);
  EXPECT_EQ(pd.get(0, pd.length()), "hello world");
}

TEST(plaindoc_tests, insert_in_middle) {
  plaindoc pd{12.0, eol::lf};
  pd.insert(0, "helloworld");
  pd.insert(5, " ");
  EXPECT_EQ(pd.get(0, pd.length()), "hello world");
}

TEST(plaindoc_tests, erase_tail) {
  plaindoc pd{12.0, eol::lf};
  pd.insert(0, "hello world");
  pd.erase(5, 6);
  EXPECT_EQ(pd.get(0, pd.length()), "hello");
}

TEST(plaindoc_tests, erase_head) {
  plaindoc pd{12.0, eol::lf};
  pd.insert(0, "hello world");
  pd.erase(0, 6);
  EXPECT_EQ(pd.get(0, pd.length()), "world");
}

TEST(plaindoc_tests, erase_middle) {
  plaindoc pd{12.0, eol::lf};
  pd.insert(0, "abcdef");
  pd.erase(2, 2);
  EXPECT_EQ(pd.get(0, pd.length()), "abef");
}

TEST(plaindoc_tests, get_substring) {
  plaindoc pd{12.0, eol::lf};
  pd.insert(0, "hello world");
  EXPECT_EQ(pd.get(0, 5), "hello");
  EXPECT_EQ(pd.get(6, 5), "world");
}

TEST(plaindoc_tests, interleaved_edits) {
  plaindoc pd{12.0, eol::lf};
  pd.insert(0, "abc");
  pd.insert(1, "XY");  // aXYbc
  EXPECT_EQ(pd.get(0, pd.length()), "aXYbc");
  pd.erase(2, 2);  // remove "Yb" -> aXc
  EXPECT_EQ(pd.get(0, pd.length()), "aXc");
  pd.insert(pd.length(), "Z");
  EXPECT_EQ(pd.get(0, pd.length()), "aXcZ");
}

TEST(plaindoc_tests, length_counts_bytes_for_multibyte) {
  plaindoc pd{12.0, eol::lf};
  pd.insert(0, "a" "\xC3\xA9" "z");  // a é z; é is 2 bytes
  EXPECT_EQ(pd.length(), 4u);
  EXPECT_EQ(pd.get(0, pd.length()), "a" "\xC3\xA9" "z");
}

TEST(plaindoc_tests, preserves_newlines) {
  plaindoc pd{12.0, eol::crlf};
  pd.insert(0, "a\r\nb\r\nc");
  EXPECT_EQ(pd.length(), 7u);
  EXPECT_EQ(pd.get(0, pd.length()), "a\r\nb\r\nc");
}

}  // namespace swg::ut::plaindoc_ut
