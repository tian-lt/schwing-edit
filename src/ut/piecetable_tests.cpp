// std
#include <string>
#include <variant>
#include <vector>

// gtest
#include <gtest/gtest.h>

// schwing
#include "piecetable.hpp"

namespace swg::ut {

namespace {
struct insert_op {
  size_t pos;
  std::string data;
};
struct erase_op {
  size_t pos;
  size_t length;
};
using test_op = std::variant<insert_op, erase_op>;

struct test_case {
  std::string original;
  std::string expected;
  std::vector<test_op> test_ops;
};
}  // namespace

struct piecetable_tests : public ::testing::TestWithParam<test_case> {};

TEST_P(piecetable_tests, run) {
  auto& p = GetParam();
  piecetable table{p.original};
  for (const auto& step : p.test_ops) {
    std::visit(
        [&]<class T>(const T& op) {
          if constexpr (std::is_same_v<T, insert_op>) {
            table.insert(op.pos, op.data);
          } else if constexpr (std::is_same_v<T, erase_op>) {
            table.erase(op.pos, op.length);
          }
        },
        step);
  }
  EXPECT_EQ(table.get(0, table.length()), p.expected);
}

INSTANTIATE_TEST_CASE_P(
    insert, piecetable_tests,
    ::testing::Values(
        // sequential appends starting from an empty buffer
        test_case{.original = "",
                  .expected = "hello world!!!",
                  .test_ops = {insert_op{.pos = 0, .data = "hello"},
                               insert_op{.pos = 5, .data = " world"},
                               insert_op{.pos = 11, .data = "!!!"}}},
        // insert in the middle of an add-buffer piece (splits it)
        test_case{
            .original = "",
            .expected = "ABC",
            .test_ops = {insert_op{.pos = 0, .data = "AC"}, insert_op{.pos = 1, .data = "B"}}},
        // no-op insertions
        test_case{.original = "abc",
                  .expected = "abc",
                  .test_ops = {insert_op{.pos = 0, .data = ""}, insert_op{.pos = 1, .data = ""},
                               insert_op{.pos = 3, .data = ""}}},
        // insert at the beginning of the original buffer
        test_case{.original = "world",
                  .expected = "hello world",
                  .test_ops = {insert_op{.pos = 0, .data = "hello "}}},
        // insert in the middle of the original buffer (splits original piece)
        test_case{.original = "helloworld",
                  .expected = "hello world",
                  .test_ops = {insert_op{.pos = 5, .data = " "}}},
        // insert at the end of the original buffer (append)
        test_case{.original = "hello",
                  .expected = "hello world",
                  .test_ops = {insert_op{.pos = 5, .data = " world"}}},
        // multiple inserts at the end coalesce into one add-buffer piece
        test_case{.original = "x",
                  .expected = "xabc",
                  .test_ops = {insert_op{.pos = 1, .data = "a"}, insert_op{.pos = 2, .data = "b"},
                               insert_op{.pos = 3, .data = "c"}}},
        // multiple inserts at the beginning (each one becomes new head)
        test_case{.original = "x",
                  .expected = "cbax",
                  .test_ops = {insert_op{.pos = 0, .data = "a"}, insert_op{.pos = 0, .data = "b"},
                               insert_op{.pos = 0, .data = "c"}}},
        // split, then split again inside the new add-buffer piece
        test_case{
            .original = "AE",
            .expected = "ABCDE",
            .test_ops = {insert_op{.pos = 1, .data = "BD"}, insert_op{.pos = 2, .data = "C"}}},
        // split, then split again inside the trailing original piece
        test_case{.original = "ABCD",
                  .expected = "AXBYCD",
                  .test_ops = {insert_op{.pos = 1, .data = "X"}, insert_op{.pos = 3, .data = "Y"}}},
        // insertions at a piece boundary coalesce with the preceding add-buffer piece
        test_case{.original = "AC",
                  .expected = "AxyC",
                  .test_ops = {insert_op{.pos = 1, .data = "x"}, insert_op{.pos = 2, .data = "y"}}},
        // building "Hello" letter-by-letter at pos 1, exercising repeated splits and
        // boundary inserts that do NOT coalesce (final piecelist has 5 add-buffer pieces)
        test_case{.original = "",
                  .expected = "Hello",
                  .test_ops = {insert_op{.pos = 0, .data = "H"}, insert_op{.pos = 1, .data = "o"},
                               insert_op{.pos = 1, .data = "l"}, insert_op{.pos = 1, .data = "l"},
                               insert_op{.pos = 1, .data = "e"}}},
        // splitting the original buffer multiple times, mixed with end and head inserts
        test_case{.original = "abcdefghij",
                  .expected = "5abc2de1fg3hij4",
                  .test_ops = {insert_op{.pos = 5, .data = "1"}, insert_op{.pos = 3, .data = "2"},
                               insert_op{.pos = 9, .data = "3"}, insert_op{.pos = 13, .data = "4"},
                               insert_op{.pos = 0, .data = "5"}}},
        // a mid-insert breaks the addbuf-adjacency chain so the next end-append must
        // NOT coalesce with the previous tail piece
        test_case{
            .original = "",
            .expected = "abXcdefghi",
            .test_ops = {insert_op{.pos = 0, .data = "abc"}, insert_op{.pos = 3, .data = "def"},
                         insert_op{.pos = 2, .data = "X"}, insert_op{.pos = 7, .data = "ghi"}}},
        // boundary insert whose preceding piece is from the original buffer
        // (must NOT coalesce; new piece is inserted in front of the existing add piece)
        test_case{.original = "AB",
                  .expected = "AYXB",
                  .test_ops = {insert_op{.pos = 1, .data = "X"}, insert_op{.pos = 1, .data = "Y"}}},
        // inserting a long run of characters in the middle of the original buffer
        test_case{.original = "AB",
                  .expected = "A" + std::string(64, 'x') + "B",
                  .test_ops = {insert_op{.pos = 1, .data = std::string(64, 'x')}}},
        // realistic editing session: build a sentence with appends, mid-inserts and a prepend-like
        // insert
        test_case{.original = "The fox",
                  .expected = "The very quick brown fox jumps over the lazy dog",
                  .test_ops = {insert_op{.pos = 4, .data = "quick "},
                               insert_op{.pos = 9, .data = " brown"},
                               insert_op{.pos = 19, .data = " jumps"},
                               insert_op{.pos = 25, .data = " over the lazy dog"},
                               insert_op{.pos = 4, .data = "very "}}},
        // alternating empty and non-empty inserts produce the same result as the
        // non-empty inserts alone (empty inserts are no-ops)
        test_case{.original = "abc",
                  .expected = "aXbYc",
                  .test_ops = {insert_op{.pos = 0, .data = ""}, insert_op{.pos = 1, .data = "X"},
                               insert_op{.pos = 2, .data = ""}, insert_op{.pos = 3, .data = "Y"},
                               insert_op{.pos = 5, .data = ""}}}));

INSTANTIATE_TEST_CASE_P(
    erase, piecetable_tests,
    ::testing::Values(
        // zero-length erase is a no-op
        test_case{.original = "hello",
                  .expected = "hello",
                  .test_ops = {erase_op{.pos = 0, .length = 0}, erase_op{.pos = 2, .length = 0},
                               erase_op{.pos = 5, .length = 0}}},
        // erase entirely inside the original buffer (splits the original piece)
        test_case{.original = "hello world",
                  .expected = "hello",
                  .test_ops = {erase_op{.pos = 5, .length = 6}}},
        // erase from the beginning of the original buffer (trims front)
        test_case{.original = "hello world",
                  .expected = "world",
                  .test_ops = {erase_op{.pos = 0, .length = 6}}},
        // erase in the middle of the original buffer (split into two)
        test_case{.original = "hello world",
                  .expected = "held",
                  .test_ops = {erase_op{.pos = 3, .length = 7}}},
        // erase the entire content
        test_case{
            .original = "hello", .expected = "", .test_ops = {erase_op{.pos = 0, .length = 5}}},
        // erase across an original/add-buffer boundary (consumes a whole add piece)
        test_case{
            .original = "AB",
            .expected = "AB",
            .test_ops = {insert_op{.pos = 1, .data = "XYZ"}, erase_op{.pos = 1, .length = 3}}},
        // erase spans multiple pieces: trims tail of one, drops a whole piece, trims head of next
        test_case{.original = "ABCDE",
                  .expected = "AE",
                  .test_ops = {insert_op{.pos = 1, .data = "1"}, insert_op{.pos = 3, .data = "2"},
                               // table is now "A1BC2DE"; erase "1BC2D" => "AE"
                               erase_op{.pos = 1, .length = 5}}},
        // erase reduces an add-buffer piece to zero (whole-piece removal)
        test_case{.original = "AB",
                  .expected = "AB",
                  .test_ops = {insert_op{.pos = 1, .data = "X"}, erase_op{.pos = 1, .length = 1}}},
        // insert, then erase the just-inserted text (split + drop)
        test_case{.original = "hello world",
                  .expected = "hello world",
                  .test_ops = {insert_op{.pos = 5, .data = " beautiful"},
                               erase_op{.pos = 5, .length = 10}}},
        // erase that crosses the boundary of an original piece into a trailing add piece
        test_case{.original = "ABCD",
                  .expected = "AY",
                  .test_ops = {insert_op{.pos = 4, .data = "XY"},
                               // table is "ABCDXY"; erase "BCDX" => "AY"
                               erase_op{.pos = 1, .length = 4}}},
        // interleaved inserts and erases
        test_case{.original = "the quick brown fox",
                  .expected = "the fox",
                  .test_ops = {erase_op{.pos = 3, .length = 12}}},
        // erase, then re-insert at the same position
        test_case{
            .original = "hello world",
            .expected = "hello C++",
            .test_ops = {erase_op{.pos = 6, .length = 5}, insert_op{.pos = 6, .data = "C++"}}},
        // erase the entire content piece by piece
        test_case{.original = "abcdef",
                  .expected = "",
                  .test_ops = {erase_op{.pos = 5, .length = 1}, erase_op{.pos = 4, .length = 1},
                               erase_op{.pos = 3, .length = 1}, erase_op{.pos = 2, .length = 1},
                               erase_op{.pos = 1, .length = 1}, erase_op{.pos = 0, .length = 1}}},
        // erase that spans many small add-buffer pieces (built from the "Hello" sequence)
        test_case{.original = "",
                  .expected = "Ho",
                  .test_ops = {insert_op{.pos = 0, .data = "H"}, insert_op{.pos = 1, .data = "o"},
                               insert_op{.pos = 1, .data = "l"}, insert_op{.pos = 1, .data = "l"},
                               insert_op{.pos = 1, .data = "e"},
                               // table is now "Hello"; erase "ell" => "Ho"
                               erase_op{.pos = 1, .length = 3}}},
        // realistic edit: build then trim
        test_case{.original = "The fox",
                  .expected = "The quick fox jumps",
                  .test_ops = {insert_op{.pos = 4, .data = "quick "},
                               insert_op{.pos = 9, .data = " brown"},
                               insert_op{.pos = 19, .data = " jumps"},
                               // table is now "The quick brown fox jumps"; erase " brown"
                               erase_op{.pos = 9, .length = 6}}}));

}  // namespace swg::ut
