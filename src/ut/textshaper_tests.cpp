// std
#include <filesystem>
#include <string>
// gtest
#include <gtest/gtest.h>
// swg
#include "fontengine.hpp"
#include "textshaper.hpp"
// ut
#include "font_env.hpp"

namespace swg::ut::textshaper_ut {

namespace {
void skip_if_no_font() {
  if (!std::filesystem::exists(arial_path())) {
    GTEST_SKIP() << "Font not found: " << arial_path();
  }
}
}  // namespace

TEST(textshaper_tests, empty_input_returns_empty) {
  textshaper shaper;
  EXPECT_TRUE(shaper.shape(nullptr, "").empty());
}

TEST(textshaper_tests, null_font_returns_empty) {
  textshaper shaper;
  EXPECT_TRUE(shaper.shape(nullptr, "Hello").empty());
}

TEST(textshaper_tests, hello_shapes_to_five_glyphs) {
  skip_if_no_font();
  fontengine fe{arial_path(), 16.0};
  textshaper shaper;
  auto glyphs = shaper.shape(fe.hbfont(), "Hello");
  ASSERT_EQ(glyphs.size(), 5u);

  int total_advance = 0;
  uint32_t prev_cluster = 0;
  for (size_t i = 0; i < glyphs.size(); ++i) {
    EXPECT_GT(glyphs[i].x_advance, 0) << "glyph " << i;
    total_advance += glyphs[i].x_advance;
    if (i > 0) {
      EXPECT_GE(glyphs[i].cluster, prev_cluster) << "monotonic clusters";
    }
    prev_cluster = glyphs[i].cluster;
  }
  EXPECT_GT(total_advance, 0);
}

TEST(textshaper_tests, reusable_buffer_produces_consistent_results) {
  skip_if_no_font();
  fontengine fe{arial_path(), 16.0};
  textshaper shaper;
  auto first = shaper.shape(fe.hbfont(), "abc");
  auto second = shaper.shape(fe.hbfont(), "abc");
  ASSERT_EQ(first.size(), second.size());
  for (size_t i = 0; i < first.size(); ++i) {
    EXPECT_EQ(first[i].id, second[i].id);
    EXPECT_EQ(first[i].x_advance, second[i].x_advance);
    EXPECT_EQ(first[i].cluster, second[i].cluster);
  }
}

}  // namespace swg::ut::textshaper_ut
