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
  bool expected_mixed = false;
  std::vector<expected_line> expected_lines;
};
}  // namespace

struct linetable_tests : ::testing::TestWithParam<test_case> {};

TEST_P(linetable_tests, run) {
  const auto& p = GetParam();
  piecetable ptable{p.content};
  linetable table{eol::lf};
  bool mixed = table.rebuild(ptable, p.mode);
  EXPECT_EQ(mixed, p.expected_mixed);
  EXPECT_EQ(table.eol_, p.mode);
  const auto& lines = table.linelist_;
  ASSERT_EQ(lines.size(), p.expected_lines.size());
  for (size_t i = 0; i < lines.size(); ++i) {
    EXPECT_EQ(lines[i].beg, p.expected_lines[i].beg) << "line index " << i;
    EXPECT_EQ(lines[i].length, p.expected_lines[i].length) << "line index " << i;
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
        test_case{.content = "hello\n",
                  .mode = eol::lf,
                  .expected_lines = {{.beg = 0, .length = 6}, {.beg = 6, .length = 0}}},
        // single line terminated by cr
        test_case{.content = "hello\r",
                  .mode = eol::cr,
                  .expected_lines = {{.beg = 0, .length = 6}, {.beg = 6, .length = 0}}},
        // single line terminated by crlf
        test_case{.content = "hello\r\n",
                  .mode = eol::crlf,
                  .expected_lines = {{.beg = 0, .length = 7}, {.beg = 7, .length = 0}}}));

INSTANTIATE_TEST_CASE_P(pure_lf, linetable_tests,
                        ::testing::Values(
                            // multiple lf-terminated lines
                            test_case{.content = "ab\ncd\nef\n",
                                      .mode = eol::lf,
                                      .expected_lines = {{.beg = 0, .length = 3},
                                                         {.beg = 3, .length = 3},
                                                         {.beg = 6, .length = 3},
                                                         {.beg = 9, .length = 0}}},
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
                                                         {.beg = 2, .length = 1},
                                                         {.beg = 3, .length = 0}}},
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
                                                         {.beg = 6, .length = 3},
                                                         {.beg = 9, .length = 0}}},
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
                                                         {.beg = 2, .length = 1},
                                                         {.beg = 3, .length = 0}}}));

INSTANTIATE_TEST_CASE_P(pure_crlf, linetable_tests,
                        ::testing::Values(
                            // multiple crlf-terminated lines
                            test_case{.content = "ab\r\ncd\r\nef\r\n",
                                      .mode = eol::crlf,
                                      .expected_lines = {{.beg = 0, .length = 4},
                                                         {.beg = 4, .length = 4},
                                                         {.beg = 8, .length = 4},
                                                         {.beg = 12, .length = 0}}},
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
                                                         {.beg = 2, .length = 2},
                                                         {.beg = 4, .length = 0}}}));

INSTANTIATE_TEST_CASE_P(
    mixed_eol, linetable_tests,
    ::testing::Values(
        // lf and cr mixed
        test_case{.content = "ab\ncd\r",
                  .mode = eol::lf,
                  .expected_mixed = true,
                  .expected_lines = {{.beg = 0, .length = 3},
                                     {.beg = 3, .length = 3},
                                     {.beg = 6, .length = 0}}},
        // lf and crlf mixed
        test_case{.content = "ab\ncd\r\n",
                  .mode = eol::lf,
                  .expected_mixed = true,
                  .expected_lines = {{.beg = 0, .length = 3},
                                     {.beg = 3, .length = 4},
                                     {.beg = 7, .length = 0}}},
        // cr and crlf mixed
        test_case{.content = "ab\rcd\r\n",
                  .mode = eol::crlf,
                  .expected_mixed = true,
                  .expected_lines = {{.beg = 0, .length = 3},
                                     {.beg = 3, .length = 4},
                                     {.beg = 7, .length = 0}}},
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
        test_case{.content = "ab\r",
                  .mode = eol::crlf,
                  .expected_lines = {{.beg = 0, .length = 3}, {.beg = 3, .length = 0}}},
        // pure-lf content but parsed in crlf mode: still splits correctly, not mixed
        // (only one terminator kind observed)
        test_case{.content = "ab\ncd\n",
                  .mode = eol::crlf,
                  .expected_mixed = false,
                  .expected_lines = {
                      {.beg = 0, .length = 3}, {.beg = 3, .length = 3}, {.beg = 6, .length = 0}}}));

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

namespace {
struct line_at_pos_case {
  std::string content;
  eol mode = eol::lf;
  // each query is (pos, expected line index)
  std::vector<std::pair<size_t, size_t>> queries;
};
}  // namespace

struct linetable_line_at_pos_tests : ::testing::TestWithParam<line_at_pos_case> {};

TEST_P(linetable_line_at_pos_tests, run) {
  const auto& p = GetParam();
  piecetable ptable{p.content};
  linetable table{eol::lf};
  table.rebuild(ptable, p.mode);
  for (const auto& [pos, expected] : p.queries) {
    EXPECT_EQ(table.line_at_pos(pos), expected) << "pos=" << pos;
  }
}

INSTANTIATE_TEST_CASE_P(
    basic, linetable_line_at_pos_tests,
    ::testing::Values(
        // empty content: any pos maps to line 0
        line_at_pos_case{.content = "", .mode = eol::lf, .queries = {{0, 0}, {1, 0}, {100, 0}}},
        // single line without terminator: every in-range pos is on line 0
        line_at_pos_case{.content = "hello",
                         .mode = eol::lf,
                         .queries = {{0, 0}, {1, 0}, {4, 0}, {5, 0}, {100, 0}}},
        // three lf-terminated lines: lines start at 0, 2, 4
        line_at_pos_case{.content = "a\nb\nc\n",
                         .mode = eol::lf,
                         .queries = {{0, 0},
                                     {1, 0},  // the '\n' belongs to line 0
                                     {2, 1},
                                     {3, 1},
                                     {4, 2},
                                     {5, 2},
                                     {6, 3},  // trailing empty line
                                     {100, 3}}},
        // leading empty line: lines start at 0, 1
        line_at_pos_case{
            .content = "\nab", .mode = eol::lf, .queries = {{0, 0}, {1, 1}, {2, 1}, {3, 1}}},
        // crlf-terminated lines: lines start at 0, 4
        line_at_pos_case{.content = "ab\r\ncd\r\n",
                         .mode = eol::crlf,
                         .queries = {{0, 0},
                                     {1, 0},
                                     {2, 0},  // '\r'
                                     {3, 0},  // '\n'
                                     {4, 1},
                                     {5, 1},
                                     {7, 1},
                                     {8, 2}}},
        // cr-terminated lines: lines start at 0, 3, 6
        line_at_pos_case{.content = "ab\rcd\ref\r",
                         .mode = eol::cr,
                         .queries = {{0, 0}, {2, 0}, {3, 1}, {5, 1}, {6, 2}, {8, 2}}},
        // mixed eols, lines start at 0, 2, 4, 7
        line_at_pos_case{
            .content = "a\nb\rc\r\nd",
            .mode = eol::lf,
            .queries = {
                {0, 0}, {1, 0}, {2, 1}, {3, 1}, {4, 2}, {5, 2}, {6, 2}, {7, 3}, {100, 3}}}));

TEST(linetable_tests, rebuild_clears_previous_lines) {
  piecetable p1{"a\nb\nc\n"};
  linetable table{eol::lf};
  EXPECT_FALSE(table.rebuild(p1, eol::lf));
  EXPECT_EQ(table.linelist_.size(), 4u);

  piecetable p2{"x\ny\n"};
  EXPECT_FALSE(table.rebuild(p2, eol::lf));
  ASSERT_EQ(table.linelist_.size(), 3u);
  EXPECT_EQ(table.linelist_[0].beg, 0u);
  EXPECT_EQ(table.linelist_[0].length, 2u);
  EXPECT_EQ(table.linelist_[1].beg, 2u);
  EXPECT_EQ(table.linelist_[1].length, 2u);
  EXPECT_EQ(table.linelist_[2].beg, 4u);
  EXPECT_EQ(table.linelist_[2].length, 0u);

  piecetable p3{""};
  EXPECT_FALSE(table.rebuild(p3, eol::lf));
  EXPECT_TRUE(table.linelist_.empty());
}

TEST(linetable_tests, rebuild_uses_piecetable_after_edits) {
  piecetable ptable{"hello"};
  ptable.insert(5, "\nworld\n");
  ptable.insert(ptable.length(), "!");
  linetable table{eol::lf};
  bool mixed = table.rebuild(ptable, eol::lf);
  EXPECT_FALSE(mixed);
  const auto& lines = table.linelist_;
  ASSERT_EQ(lines.size(), 3u);
  EXPECT_EQ(lines[0].beg, 0u);
  EXPECT_EQ(lines[0].length, 6u);  // "hello\n"
  EXPECT_EQ(lines[1].beg, 6u);
  EXPECT_EQ(lines[1].length, 6u);  // "world\n"
  EXPECT_EQ(lines[2].beg, 12u);
  EXPECT_EQ(lines[2].length, 1u);  // "!"
}

namespace {
struct insert_op_step {
  size_t pos;
  std::string data;
  bool expected_mixed = false;
};

struct insert_case {
  std::string original;
  eol mode = eol::lf;
  std::vector<insert_op_step> ops;
  std::vector<expected_line> expected_lines;
};
}  // namespace

struct linetable_insert_tests : ::testing::TestWithParam<insert_case> {};

TEST_P(linetable_insert_tests, run) {
  const auto& p = GetParam();
  piecetable ptable{p.original};
  linetable table{eol::lf};
  table.rebuild(ptable, p.mode);
  for (const auto& step : p.ops) {
    bool mixed = table.insert(step.pos, step.data);
    EXPECT_EQ(mixed, step.expected_mixed) << "pos=" << step.pos << " data=" << step.data;
  }
  const auto& lines = table.linelist_;
  ASSERT_EQ(lines.size(), p.expected_lines.size());
  for (size_t i = 0; i < lines.size(); ++i) {
    EXPECT_EQ(lines[i].beg, p.expected_lines[i].beg) << "line index " << i;
    EXPECT_EQ(lines[i].length, p.expected_lines[i].length) << "line index " << i;
  }
}

INSTANTIATE_TEST_CASE_P(no_terminator, linetable_insert_tests,
                        ::testing::Values(
                            // empty insertion is a no-op
                            insert_case{.original = "hello\n",
                                        .mode = eol::lf,
                                        .ops = {{.pos = 0, .data = ""}, {.pos = 3, .data = ""}},
                                        .expected_lines = {{.beg = 0, .length = 6},
                                                           {.beg = 6, .length = 0}}},
                            // insert in the middle of a single line: line grows, no new lines
                            insert_case{.original = "abef",
                                        .mode = eol::lf,
                                        .ops = {{.pos = 2, .data = "cd"}},
                                        .expected_lines = {{.beg = 0, .length = 6}}},
                            // insert at the very beginning of the table
                            insert_case{.original = "world",
                                        .mode = eol::lf,
                                        .ops = {{.pos = 0, .data = "hello "}},
                                        .expected_lines = {{.beg = 0, .length = 11}}},
                            // insert at the very end of the table (after last char of last line)
                            insert_case{.original = "hello",
                                        .mode = eol::lf,
                                        .ops = {{.pos = 5, .data = " world"}},
                                        .expected_lines = {{.beg = 0, .length = 11}}},
                            // insert into the middle of a multi-line document: only the host line
                            // grows, following lines are shifted right by data.size()
                            insert_case{.original = "ab\ncd\nef\n",
                                        .mode = eol::lf,
                                        .ops = {{.pos = 4, .data = "XYZ"}},
                                        .expected_lines = {{.beg = 0, .length = 3},
                                                           {.beg = 3, .length = 6},  // "cXYZd\n"
                                                           {.beg = 9, .length = 3},
                                                           {.beg = 12, .length = 0}}}));

INSTANTIATE_TEST_CASE_P(
    single_terminator, linetable_insert_tests,
    ::testing::Values(
        // insert a single lf into the middle of a single line: splits into two
        insert_case{.original = "abcd",
                    .mode = eol::lf,
                    .ops = {{.pos = 2, .data = "\n"}},
                    .expected_lines = {{.beg = 0, .length = 3}, {.beg = 3, .length = 2}}},
        // insert "X\nY" splits the host line and inserts text on both sides of the break
        insert_case{.original = "abcd",
                    .mode = eol::lf,
                    .ops = {{.pos = 2, .data = "X\nY"}},
                    .expected_lines = {{.beg = 0, .length = 4},    // "abX\n"
                                       {.beg = 4, .length = 3}}},  // "Ycd"
        // insert a lone \r into the middle of a single line
        insert_case{.original = "abcd",
                    .mode = eol::cr,
                    .ops = {{.pos = 2, .data = "\r"}},
                    .expected_lines = {{.beg = 0, .length = 3}, {.beg = 3, .length = 2}}},
        // insert \r\n into the middle of a single line
        insert_case{.original = "abcd",
                    .mode = eol::crlf,
                    .ops = {{.pos = 2, .data = "\r\n"}},
                    .expected_lines = {{.beg = 0, .length = 4}, {.beg = 4, .length = 2}}},
        // insert at the start of a line that is itself the start of the document
        insert_case{.original = "abc",
                    .mode = eol::lf,
                    .ops = {{.pos = 0, .data = "X\n"}},
                    .expected_lines = {{.beg = 0, .length = 2}, {.beg = 2, .length = 3}}},
        // insert at the end of a multi-line document (pos == total length)
        insert_case{.original = "ab\ncd",
                    .mode = eol::lf,
                    .ops = {{.pos = 5, .data = "\nef"}},
                    .expected_lines = {{.beg = 0, .length = 3},
                                       {.beg = 3, .length = 3},  // "cd\n"
                                       {.beg = 6, .length = 2}}}));

INSTANTIATE_TEST_CASE_P(multiple_terminators, linetable_insert_tests,
                        ::testing::Values(
                            // insert several lf-terminated lines into the middle of a single line
                            insert_case{.original = "abcd",
                                        .mode = eol::lf,
                                        .ops = {{.pos = 2, .data = "X\nY\nZ\n"}},
                                        .expected_lines = {{.beg = 0, .length = 4},    // "abX\n"
                                                           {.beg = 4, .length = 2},    // "Y\n"
                                                           {.beg = 6, .length = 2},    // "Z\n"
                                                           {.beg = 8, .length = 2}}},  // "cd"
                            // insert multi-line content at the beginning of a multi-line document
                            insert_case{.original = "x\ny\n",
                                        .mode = eol::lf,
                                        .ops = {{.pos = 0, .data = "a\nb\n"}},
                                        .expected_lines = {{.beg = 0, .length = 2},
                                                           {.beg = 2, .length = 2},
                                                           {.beg = 4, .length = 2},
                                                           {.beg = 6, .length = 2},
                                                           {.beg = 8, .length = 0}}},
                            // insert two crlf-terminated lines
                            insert_case{.original = "abcd",
                                        .mode = eol::crlf,
                                        .ops = {{.pos = 2, .data = "X\r\nY\r\n"}},
                                        .expected_lines = {{.beg = 0, .length = 5},  // "abX\r\n"
                                                           {.beg = 5, .length = 3},  // "Y\r\n"
                                                           {.beg = 8, .length = 2}}}));

INSTANTIATE_TEST_CASE_P(
    mixed_in_insert, linetable_insert_tests,
    ::testing::Values(
        // inserted data contains lf and bare cr -> returns true
        insert_case{.original = "abcd",
                    .mode = eol::lf,
                    .ops = {{.pos = 2, .data = "X\nY\rZ", .expected_mixed = true}},
                    .expected_lines = {{.beg = 0, .length = 4},    // "abX\n"
                                       {.beg = 4, .length = 2},    // "Y\r"
                                       {.beg = 6, .length = 3}}},  // "Zcd"
        // inserted data contains lf and crlf -> returns true
        insert_case{.original = "ab",
                    .mode = eol::lf,
                    .ops = {{.pos = 1, .data = "X\nY\r\n", .expected_mixed = true}},
                    .expected_lines = {{.beg = 0, .length = 3},    // "aX\n"
                                       {.beg = 3, .length = 3},    // "Y\r\n"
                                       {.beg = 6, .length = 1}}},  // "b"
        // inserted data ends with a bare \r (still classified as cr terminator)
        insert_case{.original = "abc",
                    .mode = eol::lf,
                    .ops = {{.pos = 1, .data = "\nX\r", .expected_mixed = true}},
                    .expected_lines = {{.beg = 0, .length = 2},      // "a\n"
                                       {.beg = 2, .length = 2},      // "X\r"
                                       {.beg = 4, .length = 2}}}));  // "bc"

INSTANTIATE_TEST_CASE_P(
    sequential_inserts, linetable_insert_tests,
    ::testing::Values(
        // build "a\nb\nc" with three appends; final state matches a fresh rebuild
        insert_case{
            .original = "",
            .mode = eol::lf,
            .ops = {{.pos = 0, .data = "a\n"}, {.pos = 2, .data = "b\n"}, {.pos = 4, .data = "c"}},
            .expected_lines = {{.beg = 0, .length = 2},
                               {.beg = 2, .length = 2},
                               {.beg = 4, .length = 1}}},
        // multiple inserts into different positions of the same line
        insert_case{.original = "abef",
                    .mode = eol::lf,
                    .ops = {{.pos = 2, .data = "cd"}, {.pos = 6, .data = "gh"}},
                    .expected_lines = {{.beg = 0, .length = 8}}},
        // insert that crosses into a later line, then another insert before it
        insert_case{.original = "ab\ncd",
                    .mode = eol::lf,
                    .ops = {{.pos = 5, .data = "\nef"}, {.pos = 0, .data = "Z\n"}},
                    .expected_lines = {{.beg = 0, .length = 2},  // "Z\n"
                                       {.beg = 2, .length = 3},  // "ab\n"
                                       {.beg = 5, .length = 3},  // "cd\n"
                                       {.beg = 8, .length = 2}}}));

// insert(pos, data) followed by a fresh rebuild must produce the same linelist_
namespace {
struct insert_vs_rebuild_case {
  std::string original;
  eol mode = eol::lf;
  size_t pos;
  std::string data;
};
}  // namespace

class linetable_insert_matches_rebuild_param
    : public ::testing::TestWithParam<insert_vs_rebuild_case> {};

TEST_P(linetable_insert_matches_rebuild_param, equivalent) {
  const auto& p = GetParam();

  // path A: rebuild on the original, then incremental insert
  piecetable ptable_a{p.original};
  linetable table_a{eol::lf};
  table_a.rebuild(ptable_a, p.mode);
  table_a.insert(p.pos, p.data);

  // path B: apply the insert via piecetable, then full rebuild
  piecetable ptable_b{p.original};
  ptable_b.insert(p.pos, p.data);
  linetable table_b{eol::lf};
  table_b.rebuild(ptable_b, p.mode);

  const auto& la = table_a.linelist_;
  const auto& lb = table_b.linelist_;
  ASSERT_EQ(la.size(), lb.size());
  for (size_t i = 0; i < la.size(); ++i) {
    EXPECT_EQ(la[i].beg, lb[i].beg) << "line " << i;
    EXPECT_EQ(la[i].length, lb[i].length) << "line " << i;
  }
}

INSTANTIATE_TEST_CASE_P(
    cases, linetable_insert_matches_rebuild_param,
    ::testing::Values(
        // insert without any terminator into a single line
        insert_vs_rebuild_case{.original = "abcdef", .mode = eol::lf, .pos = 3, .data = "XYZ"},
        // insert at start
        insert_vs_rebuild_case{.original = "abc", .mode = eol::lf, .pos = 0, .data = "X\nY\n"},
        // insert at end of last line
        insert_vs_rebuild_case{.original = "abc", .mode = eol::lf, .pos = 3, .data = "\ndef"},
        // insert into middle of a multi-line lf document, splitting the middle line
        insert_vs_rebuild_case{
            .original = "ab\ncd\nef\n", .mode = eol::lf, .pos = 4, .data = "X\nY"},
        // insert into a crlf document
        insert_vs_rebuild_case{
            .original = "ab\r\ncd\r\n", .mode = eol::crlf, .pos = 5, .data = "X\r\nY"},
        // insert that contains all three terminator kinds in cr mode
        insert_vs_rebuild_case{
            .original = "abcd", .mode = eol::cr, .pos = 2, .data = "X\nY\rZ\r\nW"}));

TEST(linetable_tests, insert_into_empty_table) {
  // Inserting into a fresh, never-rebuilt table should still work and seed line 0.
  linetable table{eol::lf};
  EXPECT_FALSE(table.insert(0, "hello"));
  ASSERT_EQ(table.linelist_.size(), 1u);
  EXPECT_EQ(table.linelist_[0].beg, 0u);
  EXPECT_EQ(table.linelist_[0].length, 5u);

  // Insert a terminator to split the seeded line.
  EXPECT_FALSE(table.insert(5, "\nworld"));
  ASSERT_EQ(table.linelist_.size(), 2u);
  EXPECT_EQ(table.linelist_[0].beg, 0u);
  EXPECT_EQ(table.linelist_[0].length, 6u);  // "hello\n"
  EXPECT_EQ(table.linelist_[1].beg, 6u);
  EXPECT_EQ(table.linelist_[1].length, 5u);  // "world"
}

TEST(linetable_tests, insert_matches_rebuild) {
  // Smoke test exercising the same equivalence at a hand-picked spot, kept as a
  // non-parametrized test so it can be debugged easily.
  piecetable ptable_a{"the quick fox"};
  linetable table_a{eol::lf};
  table_a.rebuild(ptable_a, eol::lf);
  table_a.insert(10, "brown\n");

  piecetable ptable_b{"the quick fox"};
  ptable_b.insert(10, "brown\n");
  linetable table_b{eol::lf};
  table_b.rebuild(ptable_b, eol::lf);

  const auto& la = table_a.linelist_;
  const auto& lb = table_b.linelist_;
  ASSERT_EQ(la.size(), lb.size());
  for (size_t i = 0; i < la.size(); ++i) {
    EXPECT_EQ(la[i].beg, lb[i].beg);
    EXPECT_EQ(la[i].length, lb[i].length);
  }
}

namespace {
struct erase_op_step {
  size_t pos;
  size_t length;
};

struct erase_case {
  std::string original;
  eol mode = eol::lf;
  std::vector<erase_op_step> ops;
  std::vector<expected_line> expected_lines;
};
}  // namespace

struct linetable_erase_tests : ::testing::TestWithParam<erase_case> {};

TEST_P(linetable_erase_tests, run) {
  const auto& p = GetParam();
  piecetable ptable{p.original};
  linetable table{eol::lf};
  table.rebuild(ptable, p.mode);
  for (const auto& step : p.ops) {
    table.erase(step.pos, step.length);
  }
  const auto& lines = table.linelist_;
  ASSERT_EQ(lines.size(), p.expected_lines.size());
  for (size_t i = 0; i < lines.size(); ++i) {
    EXPECT_EQ(lines[i].beg, p.expected_lines[i].beg) << "line index " << i;
    EXPECT_EQ(lines[i].length, p.expected_lines[i].length) << "line index " << i;
  }
}

INSTANTIATE_TEST_CASE_P(within_line, linetable_erase_tests,
                        ::testing::Values(
                            // erase in the middle of a single line
                            erase_case{.original = "abcdef",
                                       .mode = eol::lf,
                                       .ops = {{.pos = 2, .length = 2}},              // "cd"
                                       .expected_lines = {{.beg = 0, .length = 4}}},  // "abef"
                            // erase a prefix of the only line
                            erase_case{.original = "abcdef",
                                       .mode = eol::lf,
                                       .ops = {{.pos = 0, .length = 3}},              // "abc"
                                       .expected_lines = {{.beg = 0, .length = 3}}},  // "def"
                            // erase a suffix running to the end of the document
                            erase_case{.original = "abcdef",
                                       .mode = eol::lf,
                                       .ops = {{.pos = 3, .length = 3}},              // "def"
                                       .expected_lines = {{.beg = 0, .length = 3}}},  // "abc"
                            // zero-length erase is a no-op
                            erase_case{.original = "hello\n",
                                       .mode = eol::lf,
                                       .ops = {{.pos = 2, .length = 0}},
                                       .expected_lines = {{.beg = 0, .length = 6},
                                                          {.beg = 6, .length = 0}}}));

INSTANTIATE_TEST_CASE_P(
    merge_lines, linetable_erase_tests,
    ::testing::Values(
        // erase across one terminator merges two lines
        erase_case{.original = "ab\ncd\n",
                   .mode = eol::lf,
                   .ops = {{.pos = 1, .length = 3}},  // "b\nc"
                   .expected_lines = {{.beg = 0, .length = 3},
                                      {.beg = 3, .length = 0}}},  // "ad\n" + trailing
        // erase across two terminators merges three lines
        erase_case{.original = "ab\ncd\nef\n",
                   .mode = eol::lf,
                   .ops = {{.pos = 1, .length = 6}},  // "b\ncd\ne"
                   .expected_lines = {{.beg = 0, .length = 3},
                                      {.beg = 3, .length = 0}}},  // "af\n" + trailing
        // erasing just the terminator merges with the next line
        erase_case{.original = "ab\ncd",
                   .mode = eol::lf,
                   .ops = {{.pos = 2, .length = 1}},              // "\n"
                   .expected_lines = {{.beg = 0, .length = 4}}},  // "abcd"
        // erase a whole word leaving an empty middle line
        erase_case{.original = "ab\ncd\nef",
                   .mode = eol::lf,
                   .ops = {{.pos = 3, .length = 2}},                // "cd"
                   .expected_lines = {{.beg = 0, .length = 3},      // "ab\n"
                                      {.beg = 3, .length = 1},      // "\n"
                                      {.beg = 4, .length = 2}}}));  // "ef"

INSTANTIATE_TEST_CASE_P(whole_lines, linetable_erase_tests,
                        ::testing::Values(
                            // drop the first line
                            erase_case{.original = "ab\ncd\nef\n",
                                       .mode = eol::lf,
                                       .ops = {{.pos = 0, .length = 3}},
                                       .expected_lines = {{.beg = 0, .length = 3},
                                                          {.beg = 3, .length = 3},
                                                          {.beg = 6, .length = 0}}},
                            // drop a middle line
                            erase_case{.original = "ab\ncd\nef\n",
                                       .mode = eol::lf,
                                       .ops = {{.pos = 3, .length = 3}},
                                       .expected_lines = {{.beg = 0, .length = 3},
                                                          {.beg = 3, .length = 3},
                                                          {.beg = 6, .length = 0}}},
                            // drop the last (terminated) line
                            erase_case{.original = "ab\ncd\nef\n",
                                       .mode = eol::lf,
                                       .ops = {{.pos = 6, .length = 3}},
                                       .expected_lines = {{.beg = 0, .length = 3},
                                                          {.beg = 3, .length = 3},
                                                          {.beg = 6, .length = 0}}}));

INSTANTIATE_TEST_CASE_P(
    crlf_and_cr, linetable_erase_tests,
    ::testing::Values(
        // erase a whole "\r\n" terminator together -> clean merge
        erase_case{.original = "ab\r\ncd\r\n",
                   .mode = eol::crlf,
                   .ops = {{.pos = 2, .length = 2}},  // "\r\n"
                   .expected_lines = {{.beg = 0, .length = 6},
                                      {.beg = 6, .length = 0}}},  // "abcd\r\n" + trailing
        // drop a whole crlf-terminated line
        erase_case{.original = "ab\r\ncd\r\n",
                   .mode = eol::crlf,
                   .ops = {{.pos = 0, .length = 4}},  // "ab\r\n"
                   .expected_lines = {{.beg = 0, .length = 4},
                                      {.beg = 4, .length = 0}}},  // "cd\r\n" + trailing
        // erase a bare cr terminator -> clean merge
        erase_case{
            .original = "ab\rcd\r",
            .mode = eol::cr,
            .ops = {{.pos = 2, .length = 1}},                                         // "\r"
            .expected_lines = {{.beg = 0, .length = 5}, {.beg = 5, .length = 0}}}));  // "abcd\r"

INSTANTIATE_TEST_CASE_P(
    sequential_erases, linetable_erase_tests,
    ::testing::Values(
        // drop the first line twice
        erase_case{.original = "ab\ncd\nef\n",
                   .mode = eol::lf,
                   .ops = {{.pos = 0, .length = 3}, {.pos = 0, .length = 3}},
                   .expected_lines = {{.beg = 0, .length = 3},
                                      {.beg = 3, .length = 0}}},  // "ef\n" + trailing
        // erase two characters one after another from the same line
        erase_case{.original = "abcdef",
                   .mode = eol::lf,
                   .ops = {{.pos = 1, .length = 1}, {.pos = 1, .length = 1}},  // 'b' then 'c'
                   .expected_lines = {{.beg = 0, .length = 4}}}));             // "adef"

// erase(pos, length) followed by a fresh rebuild must produce the same linelist_.
// Cases avoid forming/exposing terminators at the cut seam (see linetable::erase docs).
namespace {
struct erase_vs_rebuild_case {
  std::string original;
  eol mode = eol::lf;
  size_t pos;
  size_t length;
};
}  // namespace

class linetable_erase_matches_rebuild_param
    : public ::testing::TestWithParam<erase_vs_rebuild_case> {};

TEST_P(linetable_erase_matches_rebuild_param, equivalent) {
  const auto& p = GetParam();

  // path A: rebuild on the original, then incremental erase
  piecetable ptable_a{p.original};
  linetable table_a{eol::lf};
  table_a.rebuild(ptable_a, p.mode);
  table_a.erase(p.pos, p.length);

  // path B: apply the erase via piecetable, then full rebuild
  piecetable ptable_b{p.original};
  ptable_b.erase(p.pos, p.length);
  linetable table_b{eol::lf};
  table_b.rebuild(ptable_b, p.mode);

  const auto& la = table_a.linelist_;
  const auto& lb = table_b.linelist_;
  ASSERT_EQ(la.size(), lb.size());
  for (size_t i = 0; i < la.size(); ++i) {
    EXPECT_EQ(la[i].beg, lb[i].beg) << "line " << i;
    EXPECT_EQ(la[i].length, lb[i].length) << "line " << i;
  }
}

INSTANTIATE_TEST_CASE_P(
    cases, linetable_erase_matches_rebuild_param,
    ::testing::Values(
        erase_vs_rebuild_case{.original = "abcdef", .mode = eol::lf, .pos = 2, .length = 2},
        erase_vs_rebuild_case{.original = "abcdef", .mode = eol::lf, .pos = 0, .length = 3},
        erase_vs_rebuild_case{.original = "abcdef", .mode = eol::lf, .pos = 3, .length = 3},
        erase_vs_rebuild_case{.original = "ab\ncd\nef\n", .mode = eol::lf, .pos = 1, .length = 3},
        erase_vs_rebuild_case{.original = "ab\ncd\nef\n", .mode = eol::lf, .pos = 1, .length = 6},
        erase_vs_rebuild_case{.original = "ab\ncd\nef\n", .mode = eol::lf, .pos = 0, .length = 3},
        erase_vs_rebuild_case{.original = "ab\ncd\nef\n", .mode = eol::lf, .pos = 3, .length = 3},
        erase_vs_rebuild_case{.original = "ab\ncd\nef\n", .mode = eol::lf, .pos = 6, .length = 3},
        erase_vs_rebuild_case{.original = "ab\ncd\nef\n", .mode = eol::lf, .pos = 0, .length = 9},
        erase_vs_rebuild_case{.original = "ab\ncd", .mode = eol::lf, .pos = 2, .length = 1},
        // crlf: erase the entire "\r\n" terminator together
        erase_vs_rebuild_case{.original = "ab\r\ncd\r\n", .mode = eol::crlf, .pos = 2, .length = 2},
        // crlf: drop a whole line
        erase_vs_rebuild_case{.original = "ab\r\ncd\r\n", .mode = eol::crlf, .pos = 0, .length = 4},
        // cr: erase a bare cr terminator
        erase_vs_rebuild_case{.original = "ab\rcd\r", .mode = eol::cr, .pos = 2, .length = 1}));

TEST(linetable_tests, erase_to_empty) {
  // Erasing every byte collapses the table to the empty state (matches rebuild on "").
  piecetable ptable{"ab\ncd\n"};
  linetable table{eol::lf};
  table.rebuild(ptable, eol::lf);
  ASSERT_EQ(table.linelist_.size(), 3u);
  table.erase(0, 6);
  EXPECT_TRUE(table.linelist_.empty());
}

TEST(linetable_tests, erase_clamps_past_end) {
  piecetable ptable{"abc"};
  linetable table{eol::lf};
  table.rebuild(ptable, eol::lf);

  // length runs past the end of the document; erase clamps to the document bounds.
  table.erase(1, 100);
  ASSERT_EQ(table.linelist_.size(), 1u);
  EXPECT_EQ(table.linelist_[0].beg, 0u);
  EXPECT_EQ(table.linelist_[0].length, 1u);  // "a"

  // erase starting at/after the end is a no-op.
  table.erase(5, 3);
  ASSERT_EQ(table.linelist_.size(), 1u);
  EXPECT_EQ(table.linelist_[0].length, 1u);

  // erasing the remaining content empties the table.
  table.erase(0, 1);
  EXPECT_TRUE(table.linelist_.empty());
}

// Tests for the bug: insert-erase-insert at document end after a terminator.
// When the document ends with a terminated line and we insert/erase/insert at the end,
// the trailing empty line invariant ensures correct line tracking.
TEST(linetable_tests, insert_erase_insert_at_end_of_terminated_line) {
  linetable table{eol::lf};

  // Simulate typing: 'a', '\n', 'b', backspace, 'b'
  table.insert(0, "a");
  ASSERT_EQ(table.linelist_.size(), 1u);

  table.insert(1, "\n");
  ASSERT_EQ(table.linelist_.size(), 2u);  // "a\n" + trailing empty
  EXPECT_EQ(table.linelist_[0].beg, 0u);
  EXPECT_EQ(table.linelist_[0].length, 2u);
  EXPECT_EQ(table.linelist_[1].beg, 2u);
  EXPECT_EQ(table.linelist_[1].length, 0u);

  table.insert(2, "b");
  ASSERT_EQ(table.linelist_.size(), 2u);  // "a\n" and "b"
  EXPECT_EQ(table.linelist_[0].length, 2u);
  EXPECT_EQ(table.linelist_[1].beg, 2u);
  EXPECT_EQ(table.linelist_[1].length, 1u);

  table.erase(2, 1);                      // backspace: remove 'b'
  ASSERT_EQ(table.linelist_.size(), 2u);  // "a\n" + trailing empty
  EXPECT_EQ(table.linelist_[0].length, 2u);
  EXPECT_EQ(table.linelist_[1].beg, 2u);
  EXPECT_EQ(table.linelist_[1].length, 0u);

  table.insert(2, "b");                   // re-type 'b'
  ASSERT_EQ(table.linelist_.size(), 2u);  // "a\n" and "b"
  EXPECT_EQ(table.linelist_[0].length, 2u);
  EXPECT_EQ(table.linelist_[1].beg, 2u);
  EXPECT_EQ(table.linelist_[1].length, 1u);
}

// Multiple insert-erase-insert cycles at various positions.
TEST(linetable_tests, insert_erase_insert_multiple_cycles) {
  piecetable ptable{"hello\nworld\n"};
  linetable table{eol::lf};
  table.rebuild(ptable, eol::lf);
  // State: [{0,6},{6,6},{12,0}]  ("hello\n", "world\n", trailing)
  ASSERT_EQ(table.linelist_.size(), 3u);

  // Erase "world\n" entirely (pos=6, len=6)
  table.erase(6, 6);
  // Should be: "hello\n" + trailing empty
  ASSERT_EQ(table.linelist_.size(), 2u);
  EXPECT_EQ(table.linelist_[0].length, 6u);
  EXPECT_EQ(table.linelist_[1].beg, 6u);
  EXPECT_EQ(table.linelist_[1].length, 0u);

  // Insert "foo" at end (pos=6)
  table.insert(6, "foo");
  // Should be: "hello\n" and "foo"
  ASSERT_EQ(table.linelist_.size(), 2u);
  EXPECT_EQ(table.linelist_[0].length, 6u);
  EXPECT_EQ(table.linelist_[1].beg, 6u);
  EXPECT_EQ(table.linelist_[1].length, 3u);

  // Insert newline at end (pos=9)
  table.insert(9, "\n");
  // Should be: "hello\n", "foo\n", trailing empty
  ASSERT_EQ(table.linelist_.size(), 3u);
  EXPECT_EQ(table.linelist_[0].length, 6u);
  EXPECT_EQ(table.linelist_[1].beg, 6u);
  EXPECT_EQ(table.linelist_[1].length, 4u);
  EXPECT_EQ(table.linelist_[2].beg, 10u);
  EXPECT_EQ(table.linelist_[2].length, 0u);

  // Erase newline at pos=9 (backspace on the terminator)
  table.erase(9, 1);
  // Should be: "hello\n", "foo"
  ASSERT_EQ(table.linelist_.size(), 2u);
  EXPECT_EQ(table.linelist_[1].length, 3u);

  // Insert "bar\n" at end (pos=9)
  table.insert(9, "bar\n");
  // Should be: "hello\n", "foobar\n", trailing empty
  ASSERT_EQ(table.linelist_.size(), 3u);
  EXPECT_EQ(table.linelist_[1].beg, 6u);
  EXPECT_EQ(table.linelist_[1].length, 7u);
  EXPECT_EQ(table.linelist_[2].beg, 13u);
  EXPECT_EQ(table.linelist_[2].length, 0u);
}

// Insert/erase at the beginning and middle of a terminated document.
TEST(linetable_tests, insert_erase_at_various_positions) {
  piecetable ptable{"ab\ncd\nef\n"};
  linetable table{eol::lf};
  table.rebuild(ptable, eol::lf);
  // [{0,3},{3,3},{6,3},{9,0}]
  ASSERT_EQ(table.linelist_.size(), 4u);

  // Insert at beginning
  table.insert(0, "X");
  // "Xab\n" "cd\n" "ef\n" trailing
  ASSERT_EQ(table.linelist_.size(), 4u);
  EXPECT_EQ(table.linelist_[0].length, 4u);

  // Erase the 'X' we just inserted
  table.erase(0, 1);
  // Back to: "ab\n" "cd\n" "ef\n" trailing
  ASSERT_EQ(table.linelist_.size(), 4u);
  EXPECT_EQ(table.linelist_[0].length, 3u);
  EXPECT_EQ(table.linelist_[1].beg, 3u);

  // Insert newline at middle of line 1 (pos=4, between 'c' and 'd')
  table.insert(4, "\n");
  // "ab\n" "c\n" "d\n" "ef\n" trailing
  ASSERT_EQ(table.linelist_.size(), 5u);
  EXPECT_EQ(table.linelist_[1].beg, 3u);
  EXPECT_EQ(table.linelist_[1].length, 2u);  // "c\n"
  EXPECT_EQ(table.linelist_[2].beg, 5u);
  EXPECT_EQ(table.linelist_[2].length, 2u);  // "d\n"

  // Erase that newline back (pos=4, len=1)
  table.erase(4, 1);
  // "ab\n" "cd\n" "ef\n" trailing
  ASSERT_EQ(table.linelist_.size(), 4u);
  EXPECT_EQ(table.linelist_[1].beg, 3u);
  EXPECT_EQ(table.linelist_[1].length, 3u);  // "cd\n"
}

// Verify that insert-erase-insert at end matches a full rebuild.
TEST(linetable_tests, insert_erase_insert_matches_rebuild) {
  // Build incrementally: type "abc\ndef\n", backspace twice (remove "f\n"),
  // then type "g\n"
  linetable table{eol::lf};
  table.insert(0, "abc\ndef\n");
  table.erase(6, 2);       // remove "f\n" -> "abc\nde"
  table.insert(6, "g\n");  // -> "abc\ndeg\n"

  piecetable ptable{"abc\ndeg\n"};
  linetable ref{eol::lf};
  ref.rebuild(ptable, eol::lf);

  ASSERT_EQ(table.linelist_.size(), ref.linelist_.size());
  for (size_t i = 0; i < table.linelist_.size(); ++i) {
    EXPECT_EQ(table.linelist_[i].beg, ref.linelist_[i].beg) << "line " << i;
    EXPECT_EQ(table.linelist_[i].length, ref.linelist_[i].length) << "line " << i;
  }
}

// Edge case: erase all content from second line onwards, then insert new content.
TEST(linetable_tests, erase_tail_then_insert) {
  piecetable ptable{"first\nsecond\nthird\n"};
  linetable table{eol::lf};
  table.rebuild(ptable, eol::lf);
  // [{0,6},{6,7},{13,6},{19,0}]
  ASSERT_EQ(table.linelist_.size(), 4u);

  // Erase everything after first line
  table.erase(6, 13);
  // "first\n" + trailing empty
  ASSERT_EQ(table.linelist_.size(), 2u);
  EXPECT_EQ(table.linelist_[0].length, 6u);
  EXPECT_EQ(table.linelist_[1].beg, 6u);
  EXPECT_EQ(table.linelist_[1].length, 0u);

  // Now insert new content after the first line
  table.insert(6, "new\n");
  // "first\n" "new\n" trailing
  ASSERT_EQ(table.linelist_.size(), 3u);
  EXPECT_EQ(table.linelist_[1].beg, 6u);
  EXPECT_EQ(table.linelist_[1].length, 4u);
  EXPECT_EQ(table.linelist_[2].beg, 10u);
  EXPECT_EQ(table.linelist_[2].length, 0u);

  // Insert more without terminator
  table.insert(10, "unterminated");
  // "first\n" "new\n" "unterminated"
  ASSERT_EQ(table.linelist_.size(), 3u);
  EXPECT_EQ(table.linelist_[2].beg, 10u);
  EXPECT_EQ(table.linelist_[2].length, 12u);
}

}  // namespace swg::ut::linetable_ut
