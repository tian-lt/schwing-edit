// std
#include <string>
#include <vector>

// gtest
#include <gtest/gtest.h>

// schwing
#include "linetable.hpp"
#include "piecetable.hpp"

namespace swg::ut::linetable_ut {

namespace {
struct expected_line {
  size_t beg = 0;
  size_t length = 0;
};

struct test_case {
  std::string content;
  eol mode = eol::lf;
  double lineheight = 1.0;
  bool expected_mixed = false;
  std::vector<expected_line> expected_lines;
};
}  // namespace

struct linetable_tests : public ::testing::TestWithParam<test_case> {};

TEST_P(linetable_tests, run) {
  const auto& p = GetParam();
  piecetable ptable{p.content};
  linetable table{eol::lf};
  bool mixed = table.rebuild(ptable, p.lineheight, p.mode);
  EXPECT_EQ(mixed, p.expected_mixed);
  EXPECT_EQ(table.get_eol(), p.mode);
  const auto& lines = table.lines();
  ASSERT_EQ(lines.size(), p.expected_lines.size());
  for (size_t i = 0; i < lines.size(); ++i) {
    EXPECT_EQ(lines[i].beg, p.expected_lines[i].beg) << "line index " << i;
    EXPECT_EQ(lines[i].length, p.expected_lines[i].length) << "line index " << i;
    EXPECT_EQ(lines[i].height, p.lineheight) << "line index " << i;
  }
}

INSTANTIATE_TEST_CASE_P(
    empty_and_basic, linetable_tests,
    ::testing::Values(
        // empty content produces no lines
        test_case{.content = "", .mode = eol::lf, .expected_lines = {}},
        test_case{.content = "", .mode = eol::cr, .expected_lines = {}},
        test_case{.content = "", .mode = eol::crlf, .expected_lines = {}},
        // single line with no terminator
        test_case{.content = "hello", .mode = eol::lf, .expected_lines = {{.beg = 0, .length = 5}}},
        // single line terminated by lf
        test_case{
            .content = "hello\n", .mode = eol::lf, .expected_lines = {{.beg = 0, .length = 6}}},
        // single line terminated by cr
        test_case{
            .content = "hello\r", .mode = eol::cr, .expected_lines = {{.beg = 0, .length = 6}}},
        // single line terminated by crlf
        test_case{.content = "hello\r\n",
                  .mode = eol::crlf,
                  .expected_lines = {{.beg = 0, .length = 7}}}));

INSTANTIATE_TEST_CASE_P(pure_lf, linetable_tests,
                        ::testing::Values(
                            // multiple lf-terminated lines
                            test_case{.content = "ab\ncd\nef\n",
                                      .mode = eol::lf,
                                      .expected_lines = {{.beg = 0, .length = 3},
                                                         {.beg = 3, .length = 3},
                                                         {.beg = 6, .length = 3}}},
                            // trailing line without terminator
                            test_case{.content = "ab\ncd\nef",
                                      .mode = eol::lf,
                                      .expected_lines = {{.beg = 0, .length = 3},
                                                         {.beg = 3, .length = 3},
                                                         {.beg = 6, .length = 2}}},
                            // empty lines (consecutive lf)
                            test_case{.content = "\n\n\n",
                                      .mode = eol::lf,
                                      .expected_lines = {{.beg = 0, .length = 1},
                                                         {.beg = 1, .length = 1},
                                                         {.beg = 2, .length = 1}}},
                            // leading empty line
                            test_case{.content = "\nab",
                                      .mode = eol::lf,
                                      .expected_lines = {{.beg = 0, .length = 1},
                                                         {.beg = 1, .length = 2}}}));

INSTANTIATE_TEST_CASE_P(pure_cr, linetable_tests,
                        ::testing::Values(
                            // multiple cr-terminated lines
                            test_case{.content = "ab\rcd\ref\r",
                                      .mode = eol::cr,
                                      .expected_lines = {{.beg = 0, .length = 3},
                                                         {.beg = 3, .length = 3},
                                                         {.beg = 6, .length = 3}}},
                            // trailing line without terminator
                            test_case{.content = "ab\rcd\ref",
                                      .mode = eol::cr,
                                      .expected_lines = {{.beg = 0, .length = 3},
                                                         {.beg = 3, .length = 3},
                                                         {.beg = 6, .length = 2}}},
                            // consecutive cr produces empty lines
                            test_case{.content = "\r\r\r",
                                      .mode = eol::cr,
                                      .expected_lines = {{.beg = 0, .length = 1},
                                                         {.beg = 1, .length = 1},
                                                         {.beg = 2, .length = 1}}}));

INSTANTIATE_TEST_CASE_P(pure_crlf, linetable_tests,
                        ::testing::Values(
                            // multiple crlf-terminated lines
                            test_case{.content = "ab\r\ncd\r\nef\r\n",
                                      .mode = eol::crlf,
                                      .expected_lines = {{.beg = 0, .length = 4},
                                                         {.beg = 4, .length = 4},
                                                         {.beg = 8, .length = 4}}},
                            // trailing line without terminator
                            test_case{.content = "ab\r\ncd\r\nef",
                                      .mode = eol::crlf,
                                      .expected_lines = {{.beg = 0, .length = 4},
                                                         {.beg = 4, .length = 4},
                                                         {.beg = 8, .length = 2}}},
                            // consecutive empty crlf lines
                            test_case{.content = "\r\n\r\n",
                                      .mode = eol::crlf,
                                      .expected_lines = {{.beg = 0, .length = 2},
                                                         {.beg = 2, .length = 2}}}));

INSTANTIATE_TEST_CASE_P(
    mixed_eol, linetable_tests,
    ::testing::Values(
        // lf and cr mixed
        test_case{.content = "ab\ncd\r",
                  .mode = eol::lf,
                  .expected_mixed = true,
                  .expected_lines = {{.beg = 0, .length = 3}, {.beg = 3, .length = 3}}},
        // lf and crlf mixed
        test_case{.content = "ab\ncd\r\n",
                  .mode = eol::lf,
                  .expected_mixed = true,
                  .expected_lines = {{.beg = 0, .length = 3}, {.beg = 3, .length = 4}}},
        // cr and crlf mixed
        test_case{.content = "ab\rcd\r\n",
                  .mode = eol::crlf,
                  .expected_mixed = true,
                  .expected_lines = {{.beg = 0, .length = 3}, {.beg = 3, .length = 4}}},
        // all three terminators present
        test_case{.content = "a\nb\rc\r\nd",
                  .mode = eol::lf,
                  .expected_mixed = true,
                  .expected_lines = {{.beg = 0, .length = 2},
                                     {.beg = 2, .length = 2},
                                     {.beg = 4, .length = 3},
                                     {.beg = 7, .length = 1}}},
        // bare cr followed by non-lf char is a cr terminator
        test_case{.content = "a\rb\nc",
                  .mode = eol::lf,
                  .expected_mixed = true,
                  .expected_lines = {{.beg = 0, .length = 2},
                                     {.beg = 2, .length = 2},
                                     {.beg = 4, .length = 1}}},
        // trailing bare cr at EOF (under crlf mode) finalizes a line as a cr terminator
        test_case{
            .content = "ab\r", .mode = eol::crlf, .expected_lines = {{.beg = 0, .length = 3}}},
        // pure-lf content but parsed in crlf mode: still splits correctly, not mixed
        // (only one terminator kind observed)
        test_case{.content = "ab\ncd\n",
                  .mode = eol::crlf,
                  .expected_mixed = false,
                  .expected_lines = {{.beg = 0, .length = 3}, {.beg = 3, .length = 3}}}));

INSTANTIATE_TEST_CASE_P(
    large, linetable_tests,
    ::testing::Values(
        // content longer than the 256-byte internal buffer so chunked reads are exercised
        test_case{.content = std::string(300, 'x') + "\n" + std::string(300, 'y'),
                  .mode = eol::lf,
                  .expected_lines = {{.beg = 0, .length = 301}, {.beg = 301, .length = 300}}},
        // crlf split that straddles the 256-byte buffer boundary
        // ('\r' lands at index 255, '\n' at index 256 -> next chunk)
        test_case{.content = std::string(255, 'a') + "\r\n" + std::string(10, 'b'),
                  .mode = eol::crlf,
                  .expected_lines = {{.beg = 0, .length = 257}, {.beg = 257, .length = 10}}}));

TEST(linetable_tests, rebuild_clears_previous_lines) {
  piecetable p1{"a\nb\nc\n"};
  linetable table{eol::lf};
  EXPECT_FALSE(table.rebuild(p1, 1.0, eol::lf));
  EXPECT_EQ(table.lines().size(), 3u);

  piecetable p2{"x\ny\n"};
  EXPECT_FALSE(table.rebuild(p2, 1.0, eol::lf));
  ASSERT_EQ(table.lines().size(), 2u);
  EXPECT_EQ(table.lines()[0].beg, 0u);
  EXPECT_EQ(table.lines()[0].length, 2u);
  EXPECT_EQ(table.lines()[1].beg, 2u);
  EXPECT_EQ(table.lines()[1].length, 2u);

  piecetable p3{""};
  EXPECT_FALSE(table.rebuild(p3, 1.0, eol::lf));
  EXPECT_TRUE(table.lines().empty());
}

TEST(linetable_tests, rebuild_uses_piecetable_after_edits) {
  piecetable ptable{"hello"};
  ptable.insert(5, "\nworld\n");
  ptable.insert(ptable.length(), "!");
  linetable table{eol::lf};
  bool mixed = table.rebuild(ptable, 2.5, eol::lf);
  EXPECT_FALSE(mixed);
  const auto& lines = table.lines();
  ASSERT_EQ(lines.size(), 3u);
  EXPECT_EQ(lines[0].beg, 0u);
  EXPECT_EQ(lines[0].length, 6u);  // "hello\n"
  EXPECT_EQ(lines[1].beg, 6u);
  EXPECT_EQ(lines[1].length, 6u);  // "world\n"
  EXPECT_EQ(lines[2].beg, 12u);
  EXPECT_EQ(lines[2].length, 1u);  // "!"
  for (const auto& l : lines) {
    EXPECT_EQ(l.height, 2.5);
  }
}

}  // namespace swg::ut::linetable_ut
