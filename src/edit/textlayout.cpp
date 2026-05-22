// std
#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
// swg
#include "textlayout.hpp"

namespace swg {

namespace {

size_t strip_eol_bytes(const piecetable& ptable, const linetable::line& ln) {
  size_t text_len = ln.length;
  if (text_len == 0) {
    return 0;
  }
  std::string trail = ptable.get(ln.beg + text_len - 1, 1);
  if (trail == "\n") {
    --text_len;
    if (text_len > 0) {
      std::string tr2 = ptable.get(ln.beg + text_len - 1, 1);
      if (tr2 == "\r") {
        --text_len;
      }
    }
  } else if (trail == "\r") {
    --text_len;
  }
  return text_len;
}

}  // namespace

layout_result layout_viewport(const piecetable& ptable, const linetable& ltable,
                              fontengine& font, textshaper& shaper, glyphatlas& atlas,
                              const layout_params& params) {
  layout_result out;
  out.line_height = font.line_height_px();
  out.ascent = font.ascent_px();
  if (out.line_height <= 0) {
    return out;
  }

  const auto lines = ltable.lines();

  // Determine the slice of visible lines.
  size_t i0 = 0;
  if (params.scroll_y > params.padding_y) {
    i0 = static_cast<size_t>((params.scroll_y - params.padding_y) / out.line_height);
  }
  size_t i1 = lines.size();
  if (params.viewport_h > 0) {
    size_t maxi = static_cast<size_t>(
        std::max(0, (params.viewport_h - params.padding_y + params.scroll_y) /
                            out.line_height + 1));
    i1 = std::min(i1, maxi);
  }

  const float u_scale = 1.0f / static_cast<float>(atlas.width());
  const float v_scale = 1.0f / static_cast<float>(atlas.height());

  for (size_t i = i0; i < i1; ++i) {
    const auto& ln = lines[i];
    size_t text_len = strip_eol_bytes(ptable, ln);
    std::string text = text_len > 0 ? ptable.get(ln.beg, text_len) : std::string{};
    auto shaped = shaper.shape(font.hbfont(), text);

    const float pen_x0 = static_cast<float>(params.padding_x);
    const float top_y =
        static_cast<float>(params.padding_y - params.scroll_y +
                           static_cast<int>(i) * out.line_height);
    const float baseline_y = top_y + static_cast<float>(out.ascent);

    float pen_x = pen_x0;
    uint32_t last_cluster = std::numeric_limits<uint32_t>::max();
    for (const auto& g : shaped) {
      if (g.cluster != last_cluster) {
        out.carets.push_back({.byte_pos = ln.beg + g.cluster,
                              .x = pen_x,
                              .baseline_y = baseline_y});
        last_cluster = g.cluster;
      }
      const glyph_info& gi = atlas.get(g.id);
      if (gi.width > 0 && gi.height > 0) {
        float gx = pen_x + static_cast<float>(g.x_offset) / 64.0f +
                   static_cast<float>(gi.bearing_x);
        float gy = baseline_y - static_cast<float>(g.y_offset) / 64.0f -
                   static_cast<float>(gi.bearing_y);
        placed_glyph pg{
            .x = gx,
            .y = gy,
            .w = static_cast<float>(gi.width),
            .h = static_cast<float>(gi.height),
            .u0 = static_cast<float>(gi.atlas_x) * u_scale,
            .v0 = static_cast<float>(gi.atlas_y) * v_scale,
            .u1 = static_cast<float>(gi.atlas_x + gi.width) * u_scale,
            .v1 = static_cast<float>(gi.atlas_y + gi.height) * v_scale,
        };
        // Cheap viewport clipping in screen X.
        if (pg.x + pg.w > 0 && pg.x < static_cast<float>(params.viewport_w)) {
          out.glyphs.push_back(pg);
        }
      }
      pen_x += static_cast<float>(g.x_advance) / 64.0f;
    }
    // Caret at the end of the line text (just before EOL).
    out.carets.push_back({.byte_pos = ln.beg + text_len,
                          .x = pen_x,
                          .baseline_y = baseline_y});
  }

  // Trailing virtual caret if the document ends with a terminator: every EOL
  // implies a (possibly empty) following line that has no `line` entry.
  if (!lines.empty()) {
    const auto& last = lines.back();
    if (last.length > 0) {
      std::string trail = ptable.get(last.beg + last.length - 1, 1);
      if (trail == "\n" || trail == "\r") {
        const float top_y = static_cast<float>(
            params.padding_y - params.scroll_y +
            static_cast<int>(lines.size()) * out.line_height);
        const float baseline_y = top_y + static_cast<float>(out.ascent);
        out.carets.push_back({.byte_pos = last.beg + last.length,
                              .x = static_cast<float>(params.padding_x),
                              .baseline_y = baseline_y});
      }
    }
  } else {
    // Empty document: still expose a caret at (0,0) so the UI has somewhere
    // to draw.
    const float baseline_y =
        static_cast<float>(params.padding_y - params.scroll_y + out.ascent);
    out.carets.push_back({.byte_pos = 0,
                          .x = static_cast<float>(params.padding_x),
                          .baseline_y = baseline_y});
  }

  return out;
}

}  // namespace swg