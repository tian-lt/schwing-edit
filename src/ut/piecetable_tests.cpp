// std
#include <cstring>
#include <optional>
#include <string>
#include <variant>
#include <vector>

// gtest
#include <gtest/gtest.h>

// schwing
#include "piecetable.hpp"

namespace swg::ut::piecetable_ut {

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
  std::optional<size_t> get_pos;
  std::optional<size_t> get_length;
  std::vector<test_op> test_ops;
};
}  // namespace

struct piecetable_tests : ::testing::TestWithParam<test_case> {};

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
  auto res1 = table.get(p.get_pos.value_or(0), p.get_length.value_or(table.length()));
  std::string res2(p.get_length.value_or(table.length()), 0);
  table.get_to(p.get_pos.value_or(0), res2);
  EXPECT_EQ(res1, p.expected);
  EXPECT_EQ(res2, p.expected);
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

INSTANTIATE_TEST_CASE_P(
    get, piecetable_tests,
    ::testing::Values(
        // read whole buffer from a pristine original-only table
        test_case{.original = "hello world", .expected = "hello world"},
        // explicit whole-buffer read with pos=0 and length=table.length()
        test_case{
            .original = "hello world", .expected = "hello world", .get_pos = 0, .get_length = 11},
        // zero-length read returns empty regardless of position
        test_case{.original = "hello", .expected = "", .get_pos = 0, .get_length = 0},
        test_case{.original = "hello", .expected = "", .get_pos = 3, .get_length = 0},
        test_case{.original = "hello", .expected = "", .get_pos = 5, .get_length = 0},
        // read prefix / suffix / middle of a single original piece
        test_case{.original = "hello world", .expected = "hello", .get_pos = 0, .get_length = 5},
        test_case{.original = "hello world", .expected = "world", .get_pos = 6, .get_length = 5},
        test_case{.original = "hello world", .expected = "lo wo", .get_pos = 3, .get_length = 5},
        // single-character reads
        test_case{.original = "abcdef", .expected = "a", .get_pos = 0, .get_length = 1},
        test_case{.original = "abcdef", .expected = "c", .get_pos = 2, .get_length = 1},
        test_case{.original = "abcdef", .expected = "f", .get_pos = 5, .get_length = 1},
        // read entirely inside an add-buffer piece created by a mid-insert
        test_case{.original = "AB",
                  .expected = "XYZ",
                  .get_pos = 1,
                  .get_length = 3,
                  .test_ops = {insert_op{.pos = 1, .data = "XYZ"}}},
        // read entirely inside the trailing original piece after a split
        test_case{.original = "ABCDEF",
                  .expected = "DEF",
                  .get_pos = 4,
                  .get_length = 3,
                  .test_ops = {insert_op{.pos = 3, .data = "X"}}},
        // read straddling original -> add boundary
        test_case{.original = "AB",
                  .expected = "BXY",
                  .get_pos = 1,
                  .get_length = 3,
                  .test_ops = {insert_op{.pos = 2, .data = "XYZ"}}},
        // read straddling add -> original boundary
        test_case{.original = "AB",
                  .expected = "YZB",
                  .get_pos = 2,
                  .get_length = 3,
                  .test_ops = {insert_op{.pos = 1, .data = "XYZ"}}},
        // read spanning original -> add -> original (three pieces)
        test_case{.original = "AB",
                  .expected = "AXYZB",
                  .get_pos = 0,
                  .get_length = 5,
                  .test_ops = {insert_op{.pos = 1, .data = "XYZ"}}},
        // read spanning many small add-buffer pieces (the "Hello" letter-by-letter table)
        test_case{.original = "",
                  .expected = "ell",
                  .get_pos = 1,
                  .get_length = 3,
                  .test_ops = {insert_op{.pos = 0, .data = "H"}, insert_op{.pos = 1, .data = "o"},
                               insert_op{.pos = 1, .data = "l"}, insert_op{.pos = 1, .data = "l"},
                               insert_op{.pos = 1, .data = "e"}}},
        // read after an erase: positions refer to the post-erase content
        // "hello world" with erase(2, 7) removes "llo wor", leaving "held"
        test_case{.original = "hello world",
                  .expected = "hel",
                  .get_pos = 0,
                  .get_length = 3,
                  .test_ops = {erase_op{.pos = 2, .length = 7}}},
        // mixed edits, read the entire final content via explicit range
        test_case{.original = "The fox",
                  .expected = "The quick fox",
                  .get_pos = 0,
                  .get_length = 13,
                  .test_ops = {insert_op{.pos = 4, .data = "quick "}}},
        // mixed edits, read a substring that crosses several pieces
        test_case{.original = "The fox",
                  .expected = "quick fox",
                  .get_pos = 4,
                  .get_length = 9,
                  .test_ops = {insert_op{.pos = 4, .data = "quick "}}},
        // read the very last byte
        test_case{.original = "abcdef",
                  .expected = "f",
                  .get_pos = 7,
                  .get_length = 1,
                  .test_ops = {insert_op{.pos = 3, .data = "XY"}}},
        // read a single byte from inside the add-buffer piece in the middle
        test_case{.original = "abcdef",
                  .expected = "Y",
                  .get_pos = 4,
                  .get_length = 1,
                  .test_ops = {insert_op{.pos = 3, .data = "XY"}}}));

// -- detach_initbuf tests --

TEST(piecetable_detach, no_op_on_empty_table) {
  piecetable t;
  EXPECT_FALSE(t.references_initbuf());
  t.detach_initbuf();
  EXPECT_EQ(t.length(), 0u);
  EXPECT_FALSE(t.references_initbuf());
}

TEST(piecetable_detach, no_op_when_all_edits_replaced_original) {
  std::string original = "hello";
  piecetable t{original};
  EXPECT_TRUE(t.references_initbuf());
  t.erase(0, 5);  // strips every original byte
  EXPECT_FALSE(t.references_initbuf());
  t.insert(0, "added");
  EXPECT_FALSE(t.references_initbuf());
  // Mutating the source after the strip should not affect the table since no
  // piece references it any more.
  std::string snapshot = t.get(0, t.length());
  std::memset(original.data(), 'X', original.size());
  EXPECT_EQ(t.get(0, t.length()), snapshot);
  t.detach_initbuf();  // still a no-op
  EXPECT_EQ(t.get(0, t.length()), snapshot);
}

TEST(piecetable_detach, copies_original_into_addbuf_and_clears_reference) {
  std::string original = "hello world";
  piecetable t{original};
  ASSERT_TRUE(t.references_initbuf());
  ASSERT_EQ(t.length(), 11u);

  t.detach_initbuf();

  EXPECT_FALSE(t.references_initbuf());
  EXPECT_EQ(t.length(), 11u);
  EXPECT_EQ(t.get(0, 11), "hello world");
  EXPECT_TRUE(t.initbuf().empty());

  // The crucial safety property: mutating / destroying the original buffer
  // after detach must not corrupt the document.
  std::memset(original.data(), 0, original.size());
  original.clear();
  original.shrink_to_fit();
  EXPECT_EQ(t.get(0, 11), "hello world");
}

TEST(piecetable_detach, preserves_content_with_mixed_pieces) {
  std::string original = "ABCDEF";
  piecetable t{original};
  t.insert(3, "xyz");     // mid-insert splits original
  t.erase(1, 1);          // removes 'B' from the first original piece
  ASSERT_EQ(t.get(0, t.length()), "ACxyzDEF");

  t.detach_initbuf();

  EXPECT_FALSE(t.references_initbuf());
  EXPECT_EQ(t.get(0, t.length()), "ACxyzDEF");

  // Subsequent edits still work and content stays correct after the source is gone.
  std::memset(original.data(), 0, original.size());
  t.insert(t.length(), "!");
  EXPECT_EQ(t.get(0, t.length()), "ACxyzDEF!");
}

TEST(piecetable_detach, length_is_unchanged) {
  piecetable t{"abcdefghij"};
  t.insert(5, "X");
  t.erase(0, 2);
  auto len_before = t.length();
  auto content_before = t.get(0, t.length());
  t.detach_initbuf();
  EXPECT_EQ(t.length(), len_before);
  EXPECT_EQ(t.get(0, t.length()), content_before);
}

}  // namespace swg::ut::piecetable_ut
