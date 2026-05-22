// std
#include <filesystem>
// gtest
#include <gtest/gtest.h>
// swg
#include "fontengine.hpp"
// ut
#include "font_env.hpp"

namespace swg::ut::fontengine_ut {

namespace {
void skip_if_no_font() {
  if (!std::filesystem::exists(arial_path())) {
    GTEST_SKIP() << "Font not found: " << arial_path();
  }
}
}  // namespace

TEST(fontengine_tests, constructs_with_arial_16px) {
  skip_if_no_font();
  fontengine fe{arial_path(), 16.0};
  EXPECT_NE(fe.face(), nullptr);
  EXPECT_NE(fe.hbfont(), nullptr);
  EXPECT_DOUBLE_EQ(fe.pixel_size(), 16.0);
}

TEST(fontengine_tests, metrics_are_positive_for_arial) {
  skip_if_no_font();
  fontengine fe{arial_path(), 16.0};
  EXPECT_GT(fe.line_height_px(), 0);
  EXPECT_GT(fe.ascent_px(), 0);
  EXPECT_GE(fe.descent_px(), 0);
  EXPECT_GT(fe.max_advance_px(), 0);
  // Sanity: ascent + descent should be no larger than the line height for
  // any sane TT font.
  EXPECT_LE(fe.ascent_px() + fe.descent_px(), fe.line_height_px() + 4);
}

TEST(fontengine_tests, larger_size_yields_larger_metrics) {
  skip_if_no_font();
  fontengine small{arial_path(), 12.0};
  fontengine large{arial_path(), 48.0};
  EXPECT_GT(large.line_height_px(), small.line_height_px());
  EXPECT_GT(large.ascent_px(), small.ascent_px());
}

TEST(fontengine_tests, throws_on_missing_font_file) {
  EXPECT_THROW(fontengine("C:\\nonexistent\\does_not_exist.ttf", 16.0),
               std::exception);
}

}  // namespace swg::ut::fontengine_ut
