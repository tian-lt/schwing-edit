// std
#include <cstdint>
#include <filesystem>
#include <fstream>
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

namespace {
struct scoped_tempfile {
  std::filesystem::path path;
  explicit scoped_tempfile(std::string_view content) {
    path = std::filesystem::temp_directory_path() /
           ("swg_ptable_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) +
            "_" + std::to_string(reinterpret_cast<uintptr_t>(this)) + ".tmp");
    std::ofstream out{path, std::ios::binary};
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
  }
  ~scoped_tempfile() {
    std::error_code ec;
    std::filesystem::remove(path, ec);
  }
};
}  // namespace

TEST(piecetable_mmap_tests, reads_mapped_file) {
  scoped_tempfile file{"hello mmap world"};
  auto table = piecetable::from_file(file.path);
  EXPECT_EQ(table.length(), 16u);
  EXPECT_EQ(table.get(0, table.length()), "hello mmap world");
  EXPECT_EQ(table.get(6, 4), "mmap");
}

TEST(piecetable_mmap_tests, edits_mapped_file) {
  scoped_tempfile file{"hello world"};
  auto table = piecetable::from_file(file.path);
  table.insert(5, " brave");
  table.erase(0, 6);
  EXPECT_EQ(table.get(0, table.length()), "brave world");
}

TEST(piecetable_mmap_tests, empty_file_is_empty_table) {
  scoped_tempfile file{""};
  auto table = piecetable::from_file(file.path);
  EXPECT_EQ(table.length(), 0u);
  EXPECT_EQ(table.get(0, 0), "");
}

#ifdef _WIN32
TEST(piecetable_mmap_tests, denies_external_writers) {
  // Only memory-mapped files keep an OS handle open with a deny-write share mode.
  // Small files are snapshotted into memory and left unlocked, so use a file at
  // the mmap threshold to exercise the lock.
  constexpr size_t mmap_threshold = 10 * 1024u * 1024u;
  scoped_tempfile file{std::string(mmap_threshold, 'x')};
  auto table = piecetable::from_file(file.path);
  std::ofstream writer{file.path, std::ios::binary | std::ios::out};
  EXPECT_FALSE(writer.is_open());
}
#endif

}  // namespace swg::ut::piecetable_ut