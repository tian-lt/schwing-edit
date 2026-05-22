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
                                     {6, 2},  // past end clamps to last line
                                     {100, 2}}},
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
                                     {8, 1}}},
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
  EXPECT_EQ(table.linelist_.size(), 3u);

  piecetable p2{"x\ny\n"};
  EXPECT_FALSE(table.rebuild(p2, eol::lf));
  ASSERT_EQ(table.linelist_.size(), 2u);
  EXPECT_EQ(table.linelist_[0].beg, 0u);
  EXPECT_EQ(table.linelist_[0].length, 2u);
  EXPECT_EQ(table.linelist_[1].beg, 2u);
  EXPECT_EQ(table.linelist_[1].length, 2u);

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
                                        .expected_lines = {{.beg = 0, .length = 6}}},
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
                                                           {.beg = 9, .length = 3}}}));

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
                                                           {.beg = 6, .length = 2}}},
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
    ptable.erase(step.pos, step.length);
    table.erase(ptable, step.pos, step.length);
  }
  const auto& lines = table.linelist_;
  ASSERT_EQ(lines.size(), p.expected_lines.size());
  for (size_t i = 0; i < lines.size(); ++i) {
    EXPECT_EQ(lines[i].beg, p.expected_lines[i].beg) << "line index " << i;
    EXPECT_EQ(lines[i].length, p.expected_lines[i].length) << "line index " << i;
  }
}

INSTANTIATE_TEST_CASE_P(
    within_single_line, linetable_erase_tests,
    ::testing::Values(
        // erase the middle of a single line: line shrinks, no line-count change
        erase_case{.original = "abcdef",
                   .mode = eol::lf,
                   .ops = {{.pos = 2, .length = 2}},
                   .expected_lines = {{.beg = 0, .length = 4}}},
        // erase from the start of a single line
        erase_case{.original = "abcdef",
                   .mode = eol::lf,
                   .ops = {{.pos = 0, .length = 3}},
                   .expected_lines = {{.beg = 0, .length = 3}}},
        // erase to the end of a single (non-terminated) line
        erase_case{.original = "abcdef",
                   .mode = eol::lf,
                   .ops = {{.pos = 3, .length = 3}},
                   .expected_lines = {{.beg = 0, .length = 3}}},
        // erase part of a non-final line in a multi-line doc shifts trailing lines
        erase_case{.original = "abcd\nefgh\nij\n",
                   .mode = eol::lf,
                   .ops = {{.pos = 1, .length = 2}},  // erase "bc"
                   .expected_lines = {{.beg = 0, .length = 3},  // "ad\n"
                                      {.beg = 3, .length = 5},  // "efgh\n"
                                      {.beg = 8, .length = 3}}}));

INSTANTIATE_TEST_CASE_P(
    cross_line, linetable_erase_tests,
    ::testing::Values(
        // erase the terminator of a line merges it with the next line
        erase_case{.original = "abc\ndef\n",
                   .mode = eol::lf,
                   .ops = {{.pos = 3, .length = 1}},  // erase first '\n'
                   .expected_lines = {{.beg = 0, .length = 7}}},  // "abcdef\n"
        // erase a whole middle line including its terminator
        erase_case{.original = "ab\ncd\nef\n",
                   .mode = eol::lf,
                   .ops = {{.pos = 3, .length = 3}},  // erase "cd\n"
                   .expected_lines = {{.beg = 0, .length = 3},  // "ab\n"
                                      {.beg = 3, .length = 3}}},
        // erase spanning multiple complete lines + parts of edge lines
        erase_case{.original = "abcd\nefgh\nij\nkl",
                   .mode = eol::lf,
                   .ops = {{.pos = 2, .length = 10}},  // erase "cd\nefgh\nij"
                   .expected_lines = {{.beg = 0, .length = 3},  // "ab\n"
                                      {.beg = 3, .length = 2}}},  // "kl"
        // erase that removes EVERYTHING
        erase_case{.original = "abc\ndef\n",
                   .mode = eol::lf,
                   .ops = {{.pos = 0, .length = 8}},
                   .expected_lines = {}}));

INSTANTIATE_TEST_CASE_P(
    crlf_boundaries, linetable_erase_tests,
    ::testing::Values(
        // erase the '\n' of a CRLF leaves a bare '\r' that is still a terminator
        erase_case{.original = "abc\r\n",
                   .mode = eol::crlf,
                   .ops = {{.pos = 4, .length = 1}},  // erase '\n'
                   .expected_lines = {{.beg = 0, .length = 4}}},  // "abc\r" (bare CR)
        // erase the '\r' of a CRLF leaves a bare '\n' that is still a terminator
        erase_case{.original = "abc\r\nxyz",
                   .mode = eol::crlf,
                   .ops = {{.pos = 3, .length = 1}},  // erase '\r'
                   .expected_lines = {{.beg = 0, .length = 4},  // "abc\n"
                                      {.beg = 4, .length = 3}}},  // "xyz"
        // erase between a bare CR and an LF: previous line's CR re-pairs with
        // the LF to form a CRLF — the line count drops by one
        erase_case{.original = "abc\rX\n",
                   .mode = eol::crlf,
                   .ops = {{.pos = 4, .length = 1}},  // erase 'X'
                   .expected_lines = {{.beg = 0, .length = 5}}},  // "abc\r\n"
        // erase that brings a stray '\r' next to a following '\n' — new CRLF
        erase_case{.original = "a\rXYZ\nb",
                   .mode = eol::lf,
                   .ops = {{.pos = 2, .length = 3}},  // erase "XYZ"
                   .expected_lines = {{.beg = 0, .length = 3},  // "a\r\n"
                                      {.beg = 3, .length = 1}}},  // "b"
        // erase a CRLF entirely
        erase_case{.original = "abc\r\ndef",
                   .mode = eol::crlf,
                   .ops = {{.pos = 3, .length = 2}},
                   .expected_lines = {{.beg = 0, .length = 6}}}));

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
  ptable_a.erase(p.pos, p.length);
  table_a.erase(ptable_a, p.pos, p.length);

  // path B: apply the erase via piecetable, then full rebuild
  piecetable ptable_b{p.original};
  ptable_b.erase(p.pos, p.length);
  linetable table_b{eol::lf};
  table_b.rebuild(ptable_b, p.mode);

  const auto& la = table_a.linelist_;
  const auto& lb = table_b.linelist_;
  ASSERT_EQ(la.size(), lb.size()) << "line count mismatch";
  for (size_t i = 0; i < la.size(); ++i) {
    EXPECT_EQ(la[i].beg, lb[i].beg) << "line " << i << " beg";
    EXPECT_EQ(la[i].length, lb[i].length) << "line " << i << " length";
    EXPECT_EQ(la[i].eol_bytes, lb[i].eol_bytes) << "line " << i << " eol_bytes";
  }
}

INSTANTIATE_TEST_CASE_P(
    cases, linetable_erase_matches_rebuild_param,
    ::testing::Values(
        // single-line interior erase
        erase_vs_rebuild_case{
            .original = "abcdef", .mode = eol::lf, .pos = 2, .length = 2},
        // erase a terminator
        erase_vs_rebuild_case{
            .original = "abc\ndef\nghi\n", .mode = eol::lf, .pos = 3, .length = 1},
        // erase a whole line including terminator
        erase_vs_rebuild_case{
            .original = "abc\ndef\nghi\n", .mode = eol::lf, .pos = 4, .length = 4},
        // erase spanning multiple lines into a middle line
        erase_vs_rebuild_case{
            .original = "abc\ndef\nghi\njkl", .mode = eol::lf, .pos = 1, .length = 8},
        // erase the LF of a CRLF
        erase_vs_rebuild_case{
            .original = "abc\r\ndef", .mode = eol::crlf, .pos = 4, .length = 1},
        // erase the CR of a CRLF
        erase_vs_rebuild_case{
            .original = "abc\r\ndef", .mode = eol::crlf, .pos = 3, .length = 1},
        // erase a single byte between a bare CR and a following LF
        erase_vs_rebuild_case{
            .original = "abc\rX\ndef", .mode = eol::crlf, .pos = 4, .length = 1},
        // erase entire doc
        erase_vs_rebuild_case{
            .original = "abc\ndef\n", .mode = eol::lf, .pos = 0, .length = 8},
        // erase tail of last line (non-terminated)
        erase_vs_rebuild_case{
            .original = "abc\ndef", .mode = eol::lf, .pos = 4, .length = 3},
        // mixed-EOL erase: remove a span that includes \r, \r\n, \n in mixed doc
        erase_vs_rebuild_case{
            .original = "a\nb\rc\r\nd", .mode = eol::lf, .pos = 2, .length = 4},
        // erase nothing (length == 0) — should be a no-op
        erase_vs_rebuild_case{
            .original = "abc\ndef", .mode = eol::lf, .pos = 2, .length = 0},
        // erase that produces a new CRLF by joining adjacent \r and \n
        erase_vs_rebuild_case{
            .original = "ab\rXYZ\ncd", .mode = eol::lf, .pos = 3, .length = 3}));

}  // namespace swg::ut::linetable_ut
