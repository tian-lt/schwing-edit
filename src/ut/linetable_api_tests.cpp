// std
#include <string>
// gtest
#include <gtest/gtest.h>
// swg
#include "linetable.hpp"
#include "piecetable.hpp"

namespace swg::ut::linetable_api_ut {

TEST(linetable_api_tests, eol_mode_reflects_construction_and_rebuild) {
  linetable t{eol::lf};
  EXPECT_EQ(t.eol_mode(), eol::lf);
  piecetable p{"hello\r\nworld"};
  t.rebuild(p, eol::crlf);
  EXPECT_EQ(t.eol_mode(), eol::crlf);
}

TEST(linetable_api_tests, lines_and_line_count_agree_after_rebuild) {
  piecetable p{"one\ntwo\nthree"};
  linetable t{eol::lf};
  t.rebuild(p, eol::lf);
  EXPECT_EQ(t.line_count(), 3u);
  auto ls = t.lines();
  ASSERT_EQ(ls.size(), 3u);
  EXPECT_EQ(ls[0].beg, 0u);
  EXPECT_EQ(ls[0].length, 4u);
  EXPECT_EQ(ls[1].beg, 4u);
  EXPECT_EQ(ls[1].length, 4u);
  EXPECT_EQ(ls[2].beg, 8u);
  EXPECT_EQ(ls[2].length, 5u);
}

TEST(linetable_api_tests, empty_document_has_zero_lines) {
  piecetable p{""};
  linetable t{eol::lf};
  t.rebuild(p, eol::lf);
  EXPECT_EQ(t.line_count(), 0u);
  EXPECT_TRUE(t.lines().empty());
}

TEST(linetable_api_tests, lines_span_is_consistent_with_internal_state) {
  piecetable p{"a\nb"};
  linetable t{eol::lf};
  t.rebuild(p, eol::lf);
  auto ls = t.lines();
  ASSERT_EQ(ls.size(), 2u);
  // Total length should equal source.
  EXPECT_EQ(ls[0].length + ls[1].length, 3u);
}

}  // namespace swg::ut::linetable_api_ut
