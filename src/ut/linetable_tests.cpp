// std
#include <string>
#include <string_view>
#include <vector>
// deps
#include <gtest/gtest.h>
// swg
#include "linetable.hpp"
#include "piecetable.hpp"

namespace swg::ut::linetable_ut {
namespace {

struct rebuild_case {
  std::string contents;
  std::vector<line> expected_lines;
  eol expected_dominant;
  bool expected_mixed;
};

struct lt_rebuild_tests : ::testing::TestWithParam<rebuild_case> {};

TEST_P(lt_rebuild_tests, run) {
  const auto& tc = GetParam();
  piecetable pt{tc.contents};
  linetable lt;
  const bool mixed = lt.rebuild(pt);
  EXPECT_EQ(mixed, tc.expected_mixed);
  EXPECT_EQ(lt.dominant_eol(), tc.expected_dominant);
  ASSERT_EQ(lt.line_count(), tc.expected_lines.size());
  for (std::size_t i = 0; i < tc.expected_lines.size(); ++i) {
    const auto got = lt.at(i);
    const auto& want = tc.expected_lines[i];
    EXPECT_EQ(got.beg, want.beg) << "line " << i;
    EXPECT_EQ(got.length, want.length) << "line " << i;
    EXPECT_EQ(got.eol_bytes, want.eol_bytes) << "line " << i;
  }
}

INSTANTIATE_TEST_SUITE_P(
    cases, lt_rebuild_tests,
    ::testing::Values(
        // empty buffer: one synthetic empty line
        rebuild_case{"", {{0, 0, 0}}, eol::lf, false},
        // single line, no terminator
        rebuild_case{"abc", {{0, 3, 0}}, eol::lf, false},
        // single line, lf
        rebuild_case{"abc\n", {{0, 4, 1}, {4, 0, 0}}, eol::lf, false},
        // two lines lf
        rebuild_case{"a\nb\n", {{0, 2, 1}, {2, 2, 1}, {4, 0, 0}}, eol::lf, false},
        // crlf
        rebuild_case{"a\r\nb\r\n", {{0, 3, 2}, {3, 3, 2}, {6, 0, 0}}, eol::crlf, false},
        // cr only
        rebuild_case{"a\rb\r", {{0, 2, 1}, {2, 2, 1}, {4, 0, 0}}, eol::cr, false},
        // mixed lf + crlf
        rebuild_case{"a\nb\r\n", {{0, 2, 1}, {2, 3, 2}, {5, 0, 0}}, eol::crlf, true},
        // bare cr at end of buffer
        rebuild_case{"abc\r", {{0, 4, 1}, {4, 0, 0}}, eol::cr, false},
        // unterminated trailing line after crlf
        rebuild_case{"a\r\nbc", {{0, 3, 2}, {3, 2, 0}}, eol::crlf, false}));

TEST(linetable_basic, line_at_pos) {
  piecetable pt{"abc\ndef\nghi"};
  linetable lt;
  lt.rebuild(pt);
  EXPECT_EQ(lt.line_at_pos(0), 0u);
  EXPECT_EQ(lt.line_at_pos(2), 0u);
  EXPECT_EQ(lt.line_at_pos(3), 0u);  // the \n belongs to line 0
  EXPECT_EQ(lt.line_at_pos(4), 1u);
  EXPECT_EQ(lt.line_at_pos(8), 2u);
  EXPECT_EQ(lt.line_at_pos(11), 2u);  // end of doc
  EXPECT_THROW(lt.line_at_pos(12), std::out_of_range);
}

struct insert_case {
  std::string initial;
  std::size_t pos;
  std::string text;
  std::string expected_after;
  std::vector<line> expected_lines;
};

struct lt_insert_tests : ::testing::TestWithParam<insert_case> {};

TEST_P(lt_insert_tests, run) {
  const auto& tc = GetParam();
  piecetable pt{tc.initial};
  linetable lt;
  lt.rebuild(pt);
  pt.insert(tc.pos, tc.text);
  lt.insert(pt, tc.pos, tc.text.size());

  // Verify line index matches a fresh rebuild.
  linetable fresh;
  fresh.rebuild(pt);
  ASSERT_EQ(pt.str(), tc.expected_after);
  ASSERT_EQ(lt.line_count(), fresh.line_count())
      << "incremental insert disagrees with fresh rebuild";
  for (std::size_t i = 0; i < lt.line_count(); ++i) {
    EXPECT_EQ(lt.at(i).beg, fresh.at(i).beg) << "line " << i << " beg";
    EXPECT_EQ(lt.at(i).length, fresh.at(i).length) << "line " << i << " length";
    EXPECT_EQ(lt.at(i).eol_bytes, fresh.at(i).eol_bytes) << "line " << i << " eol";
  }

  ASSERT_EQ(lt.line_count(), tc.expected_lines.size());
  for (std::size_t i = 0; i < tc.expected_lines.size(); ++i) {
    EXPECT_EQ(lt.at(i).beg, tc.expected_lines[i].beg);
    EXPECT_EQ(lt.at(i).length, tc.expected_lines[i].length);
    EXPECT_EQ(lt.at(i).eol_bytes, tc.expected_lines[i].eol_bytes);
  }
}

INSTANTIATE_TEST_SUITE_P(
    cases, lt_insert_tests,
    ::testing::Values(
        // append single char
        insert_case{"abc", 3, "X", "abcX", {{0, 4, 0}}},
        // insert in middle
        insert_case{"abc\ndef", 2, "ZZ", "abZZc\ndef", {{0, 6, 1}, {6, 3, 0}}},
        // insert newline splitting a line
        insert_case{"abcdef", 3, "\n", "abc\ndef", {{0, 4, 1}, {4, 3, 0}}},
        // insert crlf
        insert_case{"abcdef", 3, "\r\n", "abc\r\ndef", {{0, 5, 2}, {5, 3, 0}}},
        // insert at very start
        insert_case{"abc", 0, "X", "Xabc", {{0, 4, 0}}},
        // insert at very end of empty
        insert_case{"", 0, "X", "X", {{0, 1, 0}}},
        // CRLF merge: existing \n at start, insert \r right before
        insert_case{"\nabc", 0, "\r", "\r\nabc", {{0, 2, 2}, {2, 3, 0}}},
        // CRLF split: existing \r\n, insert text between them
        insert_case{"\r\n", 1, "x", "\rx\n", {{0, 1, 1}, {1, 2, 1}, {3, 0, 0}}}));

struct erase_case {
  std::string initial;
  std::size_t pos;
  std::size_t n;
  std::string expected_after;
};

struct lt_erase_tests : ::testing::TestWithParam<erase_case> {};

TEST_P(lt_erase_tests, run) {
  const auto& tc = GetParam();
  piecetable pt{tc.initial};
  linetable lt;
  lt.rebuild(pt);
  pt.erase(tc.pos, tc.n);
  lt.erase(pt, tc.pos, tc.n);

  EXPECT_EQ(pt.str(), tc.expected_after);

  // Verify line index matches a fresh rebuild.
  linetable fresh;
  fresh.rebuild(pt);
  ASSERT_EQ(lt.line_count(), fresh.line_count())
      << "incremental erase disagrees with fresh rebuild";
  for (std::size_t i = 0; i < lt.line_count(); ++i) {
    EXPECT_EQ(lt.at(i).beg, fresh.at(i).beg) << "line " << i << " beg";
    EXPECT_EQ(lt.at(i).length, fresh.at(i).length) << "line " << i << " length";
    EXPECT_EQ(lt.at(i).eol_bytes, fresh.at(i).eol_bytes) << "line " << i << " eol";
  }
}

INSTANTIATE_TEST_SUITE_P(
    cases, lt_erase_tests,
    ::testing::Values(
        // erase mid-line
        erase_case{"abcdef", 2, 2, "abef"},
        // erase a newline (merges two lines)
        erase_case{"abc\ndef", 3, 1, "abcdef"},
        // erase across multiple lines
        erase_case{"abc\ndef\nghi", 2, 6, "abghi"},
        // erase the \r of a CRLF
        erase_case{"abc\r\ndef", 3, 1, "abc\ndef"},
        // erase the \n of a CRLF
        erase_case{"abc\r\ndef", 4, 1, "abc\rdef"},
        // erase everything
        erase_case{"abc\ndef", 0, 7, ""}));

}  // namespace
}  // namespace swg::ut::linetable_ut
