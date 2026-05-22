// std
#include <filesystem>
// deps
#include <freetype/freetype.h>
// gtest
#include <gtest/gtest.h>
// swg
#include "fontengine.hpp"
#include "glyphatlas.hpp"
// ut
#include "font_env.hpp"

namespace swg::ut::glyphatlas_ut {

namespace {
void skip_if_no_font() {
  if (!std::filesystem::exists(arial_path())) {
    GTEST_SKIP() << "Font not found: " << arial_path();
  }
}

uint32_t glyph_for(FT_Face face, char32_t codepoint) {
  return FT_Get_Char_Index(face, codepoint);
}
}  // namespace

TEST(glyphatlas_tests, fresh_atlas_has_no_dirty_rows) {
  skip_if_no_font();
  fontengine fe{arial_path(), 16.0};
  glyphatlas atlas{fe.face()};
  EXPECT_FALSE(atlas.dirty());
  EXPECT_EQ(atlas.cached_count(), 0u);
  EXPECT_EQ(atlas.width(), glyphatlas::kAtlasSize);
  EXPECT_EQ(atlas.height(), glyphatlas::kAtlasSize);
}

TEST(glyphatlas_tests, get_rasterizes_and_caches) {
  skip_if_no_font();
  fontengine fe{arial_path(), 16.0};
  glyphatlas atlas{fe.face()};
  const uint32_t gid = glyph_for(fe.face(), U'A');
  ASSERT_NE(gid, 0u) << "no glyph for 'A' in Arial?";
  const auto& g0 = atlas.get(gid);
  EXPECT_GT(g0.advance_26_6, 0);
  EXPECT_GT(g0.width, 0u);
  EXPECT_GT(g0.height, 0u);
  EXPECT_TRUE(atlas.dirty());
  EXPECT_EQ(atlas.cached_count(), 1u);

  // Second request must hit cache — same atlas coordinates.
  const auto& g1 = atlas.get(gid);
  EXPECT_EQ(g0.atlas_x, g1.atlas_x);
  EXPECT_EQ(g0.atlas_y, g1.atlas_y);
  EXPECT_EQ(g0.width, g1.width);
  EXPECT_EQ(g0.height, g1.height);
  EXPECT_EQ(atlas.cached_count(), 1u);
}

TEST(glyphatlas_tests, clear_dirty_resets_range) {
  skip_if_no_font();
  fontengine fe{arial_path(), 16.0};
  glyphatlas atlas{fe.face()};
  (void)atlas.get(glyph_for(fe.face(), U'A'));
  EXPECT_TRUE(atlas.dirty());
  atlas.clear_dirty();
  EXPECT_FALSE(atlas.dirty());

  // A subsequent get of a fresh glyph marks dirty again.
  (void)atlas.get(glyph_for(fe.face(), U'B'));
  EXPECT_TRUE(atlas.dirty());
  auto [lo, hi] = atlas.dirty_rows();
  EXPECT_LT(lo, hi);
  EXPECT_LE(hi, glyphatlas::kAtlasSize);
}

TEST(glyphatlas_tests, distinct_glyphs_get_distinct_slots) {
  skip_if_no_font();
  fontengine fe{arial_path(), 16.0};
  glyphatlas atlas{fe.face()};
  const auto& a = atlas.get(glyph_for(fe.face(), U'A'));
  const auto& b = atlas.get(glyph_for(fe.face(), U'B'));
  const auto& c = atlas.get(glyph_for(fe.face(), U'C'));
  // At least one coordinate differs across distinct glyphs.
  EXPECT_TRUE(a.atlas_x != b.atlas_x || a.atlas_y != b.atlas_y);
  EXPECT_TRUE(b.atlas_x != c.atlas_x || b.atlas_y != c.atlas_y);
  EXPECT_EQ(atlas.cached_count(), 3u);
}

TEST(glyphatlas_tests, bitmap_size_matches_atlas_dimensions) {
  skip_if_no_font();
  fontengine fe{arial_path(), 16.0};
  glyphatlas atlas{fe.face()};
  const auto bmp = atlas.bitmap();
  EXPECT_EQ(bmp.size(),
            static_cast<size_t>(glyphatlas::kAtlasSize) * glyphatlas::kAtlasSize);
}

TEST(glyphatlas_tests, space_glyph_has_zero_size_but_positive_advance) {
  skip_if_no_font();
  fontengine fe{arial_path(), 16.0};
  glyphatlas atlas{fe.face()};
  const uint32_t gid = glyph_for(fe.face(), U' ');
  ASSERT_NE(gid, 0u);
  const auto& gi = atlas.get(gid);
  EXPECT_EQ(gi.width, 0u);
  EXPECT_EQ(gi.height, 0u);
  EXPECT_GT(gi.advance_26_6, 0);
}

}  // namespace swg::ut::glyphatlas_ut

