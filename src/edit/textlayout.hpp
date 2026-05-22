#pragma once
// std
#include <cstddef>
#include <cstdint>
#include <vector>
// swg
#include "fontengine.hpp"
#include "glyphatlas.hpp"
#include "linetable.hpp"
#include "piecetable.hpp"
#include "textshaper.hpp"

namespace swg {

// One drawable quad sampled out of the glyph atlas.
struct placed_glyph {
  float x = 0;   // viewport-space x of the quad's left edge (px)
  float y = 0;   // viewport-space y of the quad's top edge (px)
  float w = 0;   // quad width (px)
  float h = 0;   // quad height (px)
  float u0 = 0;  // atlas UV (normalized 0..1)
  float v0 = 0;
  float u1 = 0;
  float v1 = 0;
};

// Caret position cached for one byte boundary inside a visible line.
struct caret_anchor {
  size_t byte_pos = 0;  // byte offset into the document
  float x = 0;          // viewport-space x of the caret bar (px)
  float baseline_y = 0; // viewport-space baseline (px)
};

struct layout_result {
  std::vector<placed_glyph> glyphs;
  std::vector<caret_anchor> carets;
  int line_height = 0;
  int ascent = 0;
  // Maximum horizontal extent (in document space, px) reached by any line
  // touched during layout. Used by the host to size the horizontal scrollbar.
  int content_width = 0;
};

struct layout_params {
  int viewport_w = 0;
  int viewport_h = 0;
  int scroll_y = 0;      // px from top of document to top of viewport
  int scroll_x = 0;      // px from left of document to left of viewport
  int padding_x = 0;
  int padding_y = 0;
};

// Lay out the visible window of the document. Walks lines, shapes each, places
// glyphs in viewport space, and records caret anchors at every byte boundary
// touched by the visible lines. Glyphs that fall outside the viewport are
// elided; carets at line boundaries are preserved.
layout_result layout_viewport(const piecetable& ptable, const linetable& ltable,
                              fontengine& font, textshaper& shaper, glyphatlas& atlas,
                              const layout_params& params);

}  // namespace swg