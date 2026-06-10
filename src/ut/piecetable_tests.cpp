// std
#include <string>
#include <string_view>
#include <vector>
// deps
#include <gtest/gtest.h>
// swg
#include "piecetable.hpp"

namespace swg::ut::piecetable_ut {
namespace {

struct insert_case {
  std::string initbuf;
  // sequence of (pos, bytes) inserts
  std::vector<std::pair<std::size_t, std::string>> ops;
  std::string expected;
  std::size_t expected_length;
};

struct pt_insert_tests : ::testing::TestWithParam<insert_case> {};

TEST_P(pt_insert_tests, run) {
  const auto& tc = GetParam();
  piecetable pt{tc.initbuf};
  for (const auto& [pos, b] : tc.ops) {
    pt.insert(pos, b);
  }
  EXPECT_EQ(pt.length(), tc.expected_length);
  EXPECT_EQ(pt.str(), tc.expected);
}

INSTANTIATE_TEST_SUITE_P(
    cases, pt_insert_tests,
    ::testing::Values(
        // empty init, append single char
        insert_case{"", {{0, "a"}}, "a", 1},
        // append to existing
        insert_case{"hello", {{5, " world"}}, "hello world", 11},
        // prepend
        insert_case{"world", {{0, "hello "}}, "hello world", 11},
        // mid-insert (splits initial piece)
        insert_case{"helloworld", {{5, " "}}, "hello world", 11},
        // multiple coalescable typing-like inserts
        insert_case{"", {{0, "h"}, {1, "e"}, {2, "l"}, {3, "l"}, {4, "o"}}, "hello", 5},
        // insert utf-8 multibyte
        insert_case{"", {{0, "héllo"}}, "héllo", 6},
        // chained mid-insert
        insert_case{"abcdef", {{3, "XYZ"}, {3, "QQ"}}, "abcQQXYZdef", 11},
        // chained at end with coalescing
        insert_case{"abc", {{3, "1"}, {4, "2"}, {5, "3"}}, "abc123", 6}));

struct erase_case {
  std::string initbuf;
  std::vector<std::pair<std::size_t, std::string>> inserts;
  // sequence of (pos, n) erases
  std::vector<std::pair<std::size_t, std::size_t>> erases;
  std::string expected;
  std::size_t expected_length;
};

struct pt_erase_tests : ::testing::TestWithParam<erase_case> {};

TEST_P(pt_erase_tests, run) {
  const auto& tc = GetParam();
  piecetable pt{tc.initbuf};
  for (const auto& [pos, b] : tc.inserts) pt.insert(pos, b);
  for (const auto& [pos, n] : tc.erases) pt.erase(pos, n);
  EXPECT_EQ(pt.length(), tc.expected_length);
  EXPECT_EQ(pt.str(), tc.expected);
}

INSTANTIATE_TEST_SUITE_P(
    cases, pt_erase_tests,
    ::testing::Values(
        // erase from start
        erase_case{"hello world", {}, {{0, 6}}, "world", 5},
        // erase from end
        erase_case{"hello world", {}, {{5, 6}}, "hello", 5},
        // erase middle (splits piece)
        erase_case{"hello world", {}, {{5, 1}}, "helloworld", 10},
        // erase across multiple inserted pieces
        erase_case{"abcdef", {{3, "XYZ"}}, {{2, 5}}, "abef", 4},
        // erase the entire content
        erase_case{"abcdef", {}, {{0, 6}}, "", 0},
        // erase exactly one piece (drops it)
        erase_case{"abcdef", {{3, "XYZ"}}, {{3, 3}}, "abcdef", 6},
        // erase utf-8 multibyte char (2 bytes for é)
        erase_case{"héllo", {}, {{1, 2}}, "hllo", 4}));

struct get_case {
  std::string initbuf;
  std::vector<std::pair<std::size_t, std::string>> inserts;
  std::size_t pos;
  std::size_t n;
  std::string expected;
};

struct pt_get_tests : ::testing::TestWithParam<get_case> {};

TEST_P(pt_get_tests, run) {
  const auto& tc = GetParam();
  piecetable pt{tc.initbuf};
  for (const auto& [pos, b] : tc.inserts) pt.insert(pos, b);
  EXPECT_EQ(pt.get(tc.pos, tc.n), tc.expected);
}

INSTANTIATE_TEST_SUITE_P(
    cases, pt_get_tests,
    ::testing::Values(
        // full init
        get_case{"hello", {}, 0, 5, "hello"},
        // partial init
        get_case{"hello", {}, 1, 3, "ell"},
        // across pieces
        get_case{"helloworld", {{5, " "}}, 4, 3, "o w"},
        // zero-length
        get_case{"hello", {}, 2, 0, ""}));

TEST(piecetable_misc, out_of_range_throws) {
  piecetable pt{"abc"};
  EXPECT_THROW(pt.insert(10, "x"), std::out_of_range);
  EXPECT_THROW(pt.erase(2, 5), std::out_of_range);
  EXPECT_THROW(pt.get(2, 5), std::out_of_range);
}

TEST(piecetable_misc, materialize_after_initview_release) {
  std::string source = "hello world";
  piecetable pt{source};
  pt.insert(5, " brave");
  pt.materialize();
  // We can now mutate the original string; the piecetable must NOT reflect it.
  source.assign(source.size(), 'X');
  EXPECT_EQ(pt.str(), "hello brave world");
}

TEST(piecetable_misc, large_buffer_handling) {
  // 1 MB initbuf; verify insert + get work without copying the whole buffer.
  std::string big(1u << 20, 'a');
  piecetable pt{big};
  EXPECT_EQ(pt.length(), big.size());
  pt.insert(big.size() / 2, "BANG");
  EXPECT_EQ(pt.length(), big.size() + 4);
  EXPECT_EQ(pt.get(big.size() / 2, 4), "BANG");
  EXPECT_EQ(pt.get(0, 1), "a");
  EXPECT_EQ(pt.get(big.size() + 3, 1), "a");
}

}  // namespace
}  // namespace swg::ut::piecetable_ut
