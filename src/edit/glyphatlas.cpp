// std
#include <algorithm>
#include <cstring>
#include <stdexcept>
// deps
#include <freetype/freetype.h>
// swg
#include "glyphatlas.hpp"

namespace swg {

glyphatlas::glyphatlas(FT_Face face)
    : face_(face), bitmap_(static_cast<size_t>(kAtlasSize) * kAtlasSize, 0) {
  if (!face_) {
    throw std::runtime_error{"glyphatlas: null face"};
  }
}

const glyph_info& glyphatlas::get(uint32_t glyph_id) {
  if (auto it = cache_.find(glyph_id); it != cache_.end()) {
    return it->second;
  }
  glyph_info info{};
  rasterize_into(glyph_id, info);
  auto [it, inserted] = cache_.emplace(glyph_id, info);
  return it->second;
}

void glyphatlas::rasterize_into(uint32_t glyph_id, glyph_info& out) {
  check_fterror(FT_Load_Glyph(face_, glyph_id, FT_LOAD_DEFAULT | FT_LOAD_RENDER));
  const FT_GlyphSlot slot = face_->glyph;
  const FT_Bitmap& bm = slot->bitmap;

  out.bearing_x = static_cast<int16_t>(slot->bitmap_left);
  out.bearing_y = static_cast<int16_t>(slot->bitmap_top);
  out.advance_26_6 = static_cast<int32_t>(slot->advance.x);
  out.width = static_cast<uint16_t>(bm.width);
  out.height = static_cast<uint16_t>(bm.rows);

  if (bm.width == 0 || bm.rows == 0) {
    // No pixels (space, NBSP, …) — record a zero-sized cell at the current pen.
    out.atlas_x = shelf_x_;
    out.atlas_y = shelf_y_;
    return;
  }

  // Shelf allocator: wrap to next shelf when the current row can't fit the glyph.
  if (shelf_x_ + bm.width + kPadding > kAtlasSize) {
    shelf_y_ = static_cast<uint16_t>(shelf_y_ + shelf_h_ + kPadding);
    shelf_x_ = kPadding;
    shelf_h_ = 0;
  }
  if (shelf_y_ + bm.rows + kPadding > kAtlasSize) {
    throw std::runtime_error{"glyphatlas: out of atlas space"};
  }

  // Blit FT bitmap (8bpp grayscale) into the atlas at (shelf_x_, shelf_y_).
  for (unsigned int row = 0; row < bm.rows; ++row) {
    const uint8_t* src = bm.buffer + static_cast<ptrdiff_t>(row) * bm.pitch;
    uint8_t* dst = bitmap_.data() +
                   static_cast<size_t>(shelf_y_ + row) * kAtlasSize + shelf_x_;
    std::memcpy(dst, src, bm.width);
  }
  out.atlas_x = shelf_x_;
  out.atlas_y = shelf_y_;

  mark_dirty(shelf_y_, static_cast<uint16_t>(shelf_y_ + bm.rows));

  shelf_x_ = static_cast<uint16_t>(shelf_x_ + bm.width + kPadding);
  shelf_h_ = std::max<uint16_t>(shelf_h_, static_cast<uint16_t>(bm.rows));
}

void glyphatlas::mark_dirty(uint16_t y0, uint16_t y1) {
  dirty_min_y_ = std::min(dirty_min_y_, y0);
  dirty_max_y_ = std::max(dirty_max_y_, y1);
}

}  // namespace swg