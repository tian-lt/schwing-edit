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
  std::string text;
  piecetable ptable;
  linetable ltable;
  fontengine font;
  textshaper shaper;
  glyphatlas atlas;

  explicit layout_fixture(std::string text_, eol mode = eol::lf, double px = 16.0)
      : text(std::move(text_)), ptable(text), ltable(mode), font(arial_path(), px),
        atlas(font.face(), /*force_grayscale=*/true) {
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

TEST(textlayout_tests, content_width_grows_with_longer_lines) {
  skip_if_no_font();
  layout_fixture fx_short{"a"};
  layout_params params{.viewport_w = 800, .viewport_h = 600, .padding_x = 4};
  auto r_short = layout_viewport(fx_short.ptable, fx_short.ltable, fx_short.font,
                                 fx_short.shaper, fx_short.atlas, params);

  layout_fixture fx_long{"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"};
  auto r_long = layout_viewport(fx_long.ptable, fx_long.ltable, fx_long.font,
                                fx_long.shaper, fx_long.atlas, params);

  EXPECT_GT(r_long.content_width, r_short.content_width);
  // Even an empty layout should report at least the left padding so the H
  // scrollbar is never sized to 0.
  EXPECT_GE(r_short.content_width, params.padding_x);
}

TEST(textlayout_tests, content_width_is_max_over_visible_lines) {
  skip_if_no_font();
  // Three lines: short, long, short. content_width should reflect the longest.
  layout_fixture fx{"a\naaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\nb"};
  layout_params params{.viewport_w = 800, .viewport_h = 600, .padding_x = 4};
  auto r = layout_viewport(fx.ptable, fx.ltable, fx.font, fx.shaper, fx.atlas, params);
  // The middle long line should dominate; comfortably bigger than padding.
  EXPECT_GT(r.content_width, 30 * 4);
}

TEST(textlayout_tests, positive_scroll_x_shifts_glyphs_left) {
  skip_if_no_font();
  layout_fixture fx{"abcdefghijklmnop"};
  layout_params p0{.viewport_w = 800, .viewport_h = 600, .padding_x = 4};
  auto r0 = layout_viewport(fx.ptable, fx.ltable, fx.font, fx.shaper, fx.atlas, p0);
  layout_params p1 = p0;
  p1.scroll_x = 50;
  auto r1 = layout_viewport(fx.ptable, fx.ltable, fx.font, fx.shaper, fx.atlas, p1);
  // The end-of-line caret pen position should be exactly 50 px to the left
  // in r1 vs r0. Carets carry the unmodified pen_x so they're a stable
  // reference even when some left glyphs are clipped out.
  const caret_anchor* end0 = nullptr;
  const caret_anchor* end1 = nullptr;
  for (const auto& c : r0.carets) if (c.byte_pos == 16) { end0 = &c; break; }
  for (const auto& c : r1.carets) if (c.byte_pos == 16) { end1 = &c; break; }
  ASSERT_NE(end0, nullptr);
  ASSERT_NE(end1, nullptr);
  EXPECT_NEAR(end0->x - end1->x, 50.0f, 0.5f);
  // r1 should also have fewer visible glyphs than r0 because the left side
  // of the line gets clipped out of the viewport.
  EXPECT_LT(r1.glyphs.size(), r0.glyphs.size());
}

TEST(textlayout_tests, scroll_x_shifts_carets_into_screen_space) {
  skip_if_no_font();
  layout_fixture fx{"abc"};
  layout_params p0{.viewport_w = 800, .viewport_h = 600, .padding_x = 4};
  auto r0 = layout_viewport(fx.ptable, fx.ltable, fx.font, fx.shaper, fx.atlas, p0);
  layout_params p1 = p0;
  p1.scroll_x = 20;
  auto r1 = layout_viewport(fx.ptable, fx.ltable, fx.font, fx.shaper, fx.atlas, p1);
  ASSERT_GE(r0.carets.size(), 1u);
  ASSERT_GE(r1.carets.size(), 1u);
  // The byte_pos=0 caret in r0 sits at padding_x; in r1 it sits 20 px earlier.
  const caret_anchor* a0 = nullptr;
  const caret_anchor* a1 = nullptr;
  for (const auto& c : r0.carets) if (c.byte_pos == 0) { a0 = &c; break; }
  for (const auto& c : r1.carets) if (c.byte_pos == 0) { a1 = &c; break; }
  ASSERT_NE(a0, nullptr);
  ASSERT_NE(a1, nullptr);
  EXPECT_NEAR(a0->x - a1->x, 20.0f, 0.5f);
}

}  // namespace swg::ut::textlayout_ut
