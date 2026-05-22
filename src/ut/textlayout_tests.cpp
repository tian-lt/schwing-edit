// std
#include <filesystem>
#include <string>
// gtest
#include <gtest/gtest.h>
// swg
#include "fontengine.hpp"
#include "glyphatlas.hpp"
#include "linetable.hpp"
#include "piecetable.hpp"
#include "textlayout.hpp"
#include "textshaper.hpp"
// ut
#include "font_env.hpp"

namespace swg::ut::textlayout_ut {

namespace {
void skip_if_no_font() {
  if (!std::filesystem::exists(arial_path())) {
    GTEST_SKIP() << "Font not found: " << arial_path();
  }
}

struct layout_fixture {
  piecetable ptable;
  linetable ltable;
  fontengine font;
  textshaper shaper;
  glyphatlas atlas;

  explicit layout_fixture(std::string text, eol mode = eol::lf, double px = 16.0)
      : ptable(std::move(text)), ltable(mode), font(arial_path(), px), atlas(font.face()) {
    ltable.rebuild(ptable, mode);
  }
};
}  // namespace

TEST(textlayout_tests, empty_document_has_one_caret_anchor) {
  skip_if_no_font();
  layout_fixture fx{""};
  layout_params params{.viewport_w = 800, .viewport_h = 600};
  auto r = layout_viewport(fx.ptable, fx.ltable, fx.font, fx.shaper, fx.atlas, params);
  EXPECT_TRUE(r.glyphs.empty());
  ASSERT_EQ(r.carets.size(), 1u);
  EXPECT_EQ(r.carets[0].byte_pos, 0u);
  EXPECT_GT(r.line_height, 0);
  EXPECT_GT(r.ascent, 0);
}

TEST(textlayout_tests, single_line_glyph_count_matches_chars) {
  skip_if_no_font();
  layout_fixture fx{"abc"};
  layout_params params{.viewport_w = 800, .viewport_h = 600};
  auto r = layout_viewport(fx.ptable, fx.ltable, fx.font, fx.shaper, fx.atlas, params);
  EXPECT_EQ(r.glyphs.size(), 3u);
  // Caret anchors: one per cluster + one at end of line = 4
  EXPECT_GE(r.carets.size(), 4u);
}

TEST(textlayout_tests, two_lines_are_vertically_spaced_by_line_height) {
  skip_if_no_font();
  layout_fixture fx{"abc\ndef"};
  layout_params params{.viewport_w = 800, .viewport_h = 600};
  auto r = layout_viewport(fx.ptable, fx.ltable, fx.font, fx.shaper, fx.atlas, params);
  ASSERT_EQ(r.glyphs.size(), 6u);
  // Find baselines used: each line should share a y in carets.
  // The first 3 glyphs come from line 0, next 3 from line 1.
  EXPECT_LT(r.glyphs[0].y, r.glyphs[3].y);
  // Difference should equal line_height (up to rounding/bearing).
  float dy = r.glyphs[3].y - r.glyphs[0].y;
  EXPECT_NEAR(dy, static_cast<float>(r.line_height), 4.0f);
}

TEST(textlayout_tests, caret_at_byte_after_lf_is_on_second_line) {
  skip_if_no_font();
  layout_fixture fx{"abc\ndef"};
  layout_params params{.viewport_w = 800, .viewport_h = 600};
  auto r = layout_viewport(fx.ptable, fx.ltable, fx.font, fx.shaper, fx.atlas, params);

  const caret_anchor* line1_first = nullptr;
  const caret_anchor* line0_first = nullptr;
  for (const auto& c : r.carets) {
    if (c.byte_pos == 0 && !line0_first) {
      line0_first = &c;
    }
    if (c.byte_pos == 4 && !line1_first) {
      line1_first = &c;
    }
  }
  ASSERT_NE(line0_first, nullptr);
  ASSERT_NE(line1_first, nullptr);
  EXPECT_LT(line0_first->baseline_y, line1_first->baseline_y);
}

TEST(textlayout_tests, trailing_eol_emits_virtual_caret_on_next_line) {
  skip_if_no_font();
  layout_fixture fx{"abc\n"};
  layout_params params{.viewport_w = 800, .viewport_h = 600};
  auto r = layout_viewport(fx.ptable, fx.ltable, fx.font, fx.shaper, fx.atlas, params);

  // Doc length is 4 bytes ("abc\n"). The caret for the empty line after the
  // terminator should sit at byte 4.
  bool found_virtual = false;
  for (const auto& c : r.carets) {
    if (c.byte_pos == 4) {
      found_virtual = true;
      break;
    }
  }
  EXPECT_TRUE(found_virtual);
}

TEST(textlayout_tests, crlf_line_terminator_does_not_produce_extra_glyphs) {
  skip_if_no_font();
  layout_fixture fx{"abc\r\ndef", eol::crlf};
  layout_params params{.viewport_w = 800, .viewport_h = 600};
  auto r = layout_viewport(fx.ptable, fx.ltable, fx.font, fx.shaper, fx.atlas, params);
  // 3 + 3 visible glyphs only; CR and LF are stripped.
  EXPECT_EQ(r.glyphs.size(), 6u);
}

TEST(textlayout_tests, viewport_clipping_drops_horizontally_offscreen_glyphs) {
  skip_if_no_font();
  layout_fixture fx{"abcdefghijklmnopqrstuvwxyz"};
  layout_params params{.viewport_w = 10, .viewport_h = 600};
  auto r = layout_viewport(fx.ptable, fx.ltable, fx.font, fx.shaper, fx.atlas, params);
  // Some glyphs must be clipped out.
  EXPECT_LT(r.glyphs.size(), 26u);
}

}  // namespace swg::ut::textlayout_ut
