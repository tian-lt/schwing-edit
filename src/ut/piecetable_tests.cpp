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
using test_op = std::variant<insert_op>;

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
        test_case{.original = "",
                  .expected = "ABC",
                  .test_ops = {insert_op{.pos = 0, .data = "AC"},
                               insert_op{.pos = 1, .data = "B"}}},
        // no-op insertions
        test_case{.original = "abc",
                  .expected = "abc",
                  .test_ops = {insert_op{.pos = 0, .data = ""},
                               insert_op{.pos = 1, .data = ""},
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
                  .test_ops = {insert_op{.pos = 1, .data = "a"},
                               insert_op{.pos = 2, .data = "b"},
                               insert_op{.pos = 3, .data = "c"}}},
        // multiple inserts at the beginning (each one becomes new head)
        test_case{.original = "x",
                  .expected = "cbax",
                  .test_ops = {insert_op{.pos = 0, .data = "a"},
                               insert_op{.pos = 0, .data = "b"},
                               insert_op{.pos = 0, .data = "c"}}},
        // split, then split again inside the new add-buffer piece
        test_case{.original = "AE",
                  .expected = "ABCDE",
                  .test_ops = {insert_op{.pos = 1, .data = "BD"},
                               insert_op{.pos = 2, .data = "C"}}},
        // split, then split again inside the trailing original piece
        test_case{.original = "ABCD",
                  .expected = "AXBYCD",
                  .test_ops = {insert_op{.pos = 1, .data = "X"},
                               insert_op{.pos = 3, .data = "Y"}}},
        // insertions at a piece boundary coalesce with the preceding add-buffer piece
        test_case{.original = "AC",
                  .expected = "AxyC",
                  .test_ops = {insert_op{.pos = 1, .data = "x"},
                               insert_op{.pos = 2, .data = "y"}}}));

}  // namespace swg::ut
