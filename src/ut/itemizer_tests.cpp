// std
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

// gtest
#include <gtest/gtest.h>

// icu
#include <unicode/uscript.h>

// swg
#include "itemizer.hpp"

namespace swg::ut {

namespace {

constexpr std::u8string_view mixed_text =
    u8"Hello, 世界！こんにちは मनीष مرحبا ABC123 안녕하세요 俳句";

std::string_view as_bytes(std::u8string_view text) {
  return std::string_view{reinterpret_cast<const char*>(text.data()), text.length()};
}

// Feeds the given byte chunks through an itemizer and returns the emitted runs.
std::vector<scriptrun> itemize_chunks(std::string_view bytes,
                                      const std::vector<size_t>& chunk_sizes) {
  std::vector<scriptrun> runs;
  itemizer itz{[&](scriptrun run) { runs.push_back(run); }};
  size_t pos = 0;
  for (size_t size : chunk_sizes) {
    if (pos >= bytes.length()) {
      break;
    }
    size_t take = std::min(size, bytes.length() - pos);
    itz.feed(bytes.substr(pos, take));
    pos += take;
  }
  if (pos < bytes.length()) {
    itz.feed(bytes.substr(pos));
  }
  itz.finish();
  return runs;
}

// Verifies that the runs cover [0, total) contiguously with no gaps or overlaps,
// in both byte space and position (code point) space.
void expect_contiguous(const std::vector<scriptrun>& runs, size_t total_bytes, size_t total_pos) {
  size_t byte_cursor = 0;
  size_t pos_cursor = 0;
  for (size_t i = 0; i < runs.size(); ++i) {
    EXPECT_EQ(runs[i].byte_offset, byte_cursor) << "byte_offset at run " << i;
    EXPECT_EQ(runs[i].pos_offset, pos_cursor) << "pos_offset at run " << i;
    EXPECT_GT(runs[i].byte_length, 0u) << "empty byte run " << i;
    EXPECT_GT(runs[i].pos_length, 0u) << "empty pos run " << i;
    byte_cursor += runs[i].byte_length;
    pos_cursor += runs[i].pos_length;
  }
  EXPECT_EQ(byte_cursor, total_bytes);
  EXPECT_EQ(pos_cursor, total_pos);
}

struct expected_run {
  UScriptCode script_code = USCRIPT_INVALID_CODE;
  size_t byte_length = 0;
  size_t pos_length = 0;
};

const std::vector<expected_run>& expected_mixed_runs() {
  static const std::vector<expected_run> runs = {
      {USCRIPT_LATIN, 7, 7},        // "Hello, " (Latin + COMMON comma/space)
      {USCRIPT_HAN, 9, 3},          // "世界！" (Han + COMMON ！)
      {USCRIPT_HIRAGANA, 16, 6},    // "こんにちは "
      {USCRIPT_DEVANAGARI, 13, 5},  // "मनीष "
      {USCRIPT_ARABIC, 11, 6},      // "مرحبا "
      {USCRIPT_LATIN, 7, 7},        // "ABC123 "
      {USCRIPT_HANGUL, 16, 6},      // "안녕하세요 "
      {USCRIPT_HAN, 6, 2},          // "俳句"
  };
  return runs;
}

void expect_matches_mixed(const std::vector<scriptrun>& runs) {
  const auto& expected = expected_mixed_runs();
  ASSERT_EQ(runs.size(), expected.size());
  for (size_t i = 0; i < runs.size(); ++i) {
    EXPECT_EQ(runs[i].script_code, expected[i].script_code) << "script at run " << i;
    EXPECT_EQ(runs[i].byte_length, expected[i].byte_length) << "byte_length at run " << i;
    EXPECT_EQ(runs[i].pos_length, expected[i].pos_length) << "pos_length at run " << i;
  }
  expect_contiguous(runs, mixed_text.length(), 42);
}

}  // namespace

TEST(itemizer_tests, runbreaking) {
  std::vector<scriptrun> runs;
  itemizer itz{[&](scriptrun run) { runs.push_back(run); }};
  itz.feed(as_bytes(mixed_text));
  itz.finish();
  expect_matches_mixed(runs);
}

TEST(itemizer_tests, runbreaking_with_chunks) {
  // Same input split across multiple feed() calls of varying sizes. The chunk
  // boundaries deliberately land in the middle of multi-byte characters so the
  // pending buffer must stitch them back together.
  const auto bytes = as_bytes(mixed_text);
  auto runs = itemize_chunks(bytes, {1, 3, 5, 2, 7, 4, 11, 1, 6});
  expect_matches_mixed(runs);
}

TEST(itemizer_tests, runbreaking_byte_by_byte) {
  // Feeding one byte at a time exercises the worst case for breaking in the
  // middle of every multi-byte character; the result must be identical.
  const auto bytes = as_bytes(mixed_text);
  std::vector<size_t> ones(bytes.length(), 1);
  auto runs = itemize_chunks(bytes, ones);
  expect_matches_mixed(runs);
}

TEST(itemizer_tests, empty_input_emits_nothing) {
  std::vector<scriptrun> runs;
  itemizer itz{[&](scriptrun run) { runs.push_back(run); }};
  itz.feed(std::string_view{});
  itz.finish();
  EXPECT_TRUE(runs.empty());
}

TEST(itemizer_tests, common_only_input_is_single_run) {
  // A string of only COMMON characters never resolves to a real script but must
  // still be reported as one contiguous run.
  const std::u8string_view text = u8"12345 .,!?";
  std::vector<scriptrun> runs;
  itemizer itz{[&](scriptrun run) { runs.push_back(run); }};
  itz.feed(as_bytes(text));
  itz.finish();
  ASSERT_EQ(runs.size(), 1u);
  EXPECT_EQ(runs[0].byte_offset, 0u);
  EXPECT_EQ(runs[0].byte_length, text.length());
  EXPECT_EQ(runs[0].pos_length, text.length());
}

TEST(itemizer_tests, flush_carries_script_to_following_common) {
  // Splitting "世界；世界" with flush() right before the fullwidth ； must not
  // orphan the COMMON ； into its own run: every run stays HAN so the punctuation
  // is shaped with the CJK font instead of rendering as tofu.
  const auto bytes = as_bytes(u8"世界；世界");
  std::vector<scriptrun> runs;
  itemizer itz{[&](scriptrun run) { runs.push_back(run); }};
  itz.feed(bytes.substr(0, 6));  // "世界"
  itz.flush();
  itz.feed(bytes.substr(6));  // "；世界"
  itz.finish();
  ASSERT_FALSE(runs.empty());
  for (const auto& r : runs) {
    EXPECT_EQ(r.script_code, USCRIPT_HAN);
  }
  expect_contiguous(runs, bytes.length(), 5);
}

TEST(itemizer_tests, finish_carries_script_across_separator) {
  // A separator boundary (here a tab) must also carry script context, so a lone
  // ； following the tab is still resolved as HAN rather than orphaned COMMON.
  const auto bytes = as_bytes(u8"世界\t；");
  std::vector<scriptrun> runs;
  itemizer itz{[&](scriptrun run) { runs.push_back(run); }};
  itz.feed(bytes.substr(0, 7));  // "世界\t"
  itz.finish();
  itz.feed(bytes.substr(7));  // "；"
  itz.finish();
  ASSERT_FALSE(runs.empty());
  for (const auto& r : runs) {
    EXPECT_EQ(r.script_code, USCRIPT_HAN);
  }
}

}  // namespace swg::ut
