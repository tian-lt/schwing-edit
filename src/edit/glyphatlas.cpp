// std
#include <algorithm>
#include <cstring>
#include <stdexcept>
// deps
#include <freetype/freetype.h>
#include FT_LCD_FILTER_H
// swg
#include "glyphatlas.hpp"

namespace swg {

glyphatlas::glyphatlas(FT_Face face, bool force_grayscale)
    : face_(face),
      mode_(!force_grayscale && ft_lcd_filter_available() ? render_mode::lcd_rgb
                                                          : render_mode::grayscale),
      bitmap_(static_cast<size_t>(kAtlasSize) * kAtlasSize *
                  static_cast<size_t>(mode_),
              0) {
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
  // For LCD mode the loader returns a bitmap that is 3x as wide as the
  // visual glyph, with subpixel R/G/B data packed horizontally. The pixel
  // mode flag in the slot tells us what we actually got — old FreeType
  // builds without LCD filtering will silently fall back to FT_PIXEL_MODE_GRAY.
  FT_Int32 flags = FT_LOAD_RENDER;
  if (mode_ == render_mode::lcd_rgb) flags |= FT_LOAD_TARGET_LCD;
  check_fterror(FT_Load_Glyph(face_, glyph_id, flags));
  const FT_GlyphSlot slot = face_->glyph;
  const FT_Bitmap& bm = slot->bitmap;

  // visual_w is the on-screen width of the glyph in whole pixels. For LCD
  // bitmaps this is bm.width / 3; for grayscale it equals bm.width.
  const uint16_t visual_w =
      (bm.pixel_mode == FT_PIXEL_MODE_LCD)
          ? static_cast<uint16_t>(bm.width / 3)
          : static_cast<uint16_t>(bm.width);

  out.bearing_x = static_cast<int16_t>(slot->bitmap_left);
  out.bearing_y = static_cast<int16_t>(slot->bitmap_top);
  out.advance_26_6 = static_cast<int32_t>(slot->advance.x);
  out.width = visual_w;
  out.height = static_cast<uint16_t>(bm.rows);

  if (visual_w == 0 || bm.rows == 0) {
    // No pixels (space, NBSP, …) — record a zero-sized cell at the current pen.
    out.atlas_x = shelf_x_;
    out.atlas_y = shelf_y_;
    return;
  }

  // Shelf allocator: wrap to next shelf when the current row can't fit the glyph.
  if (shelf_x_ + visual_w + kPadding > kAtlasSize) {
    shelf_y_ = static_cast<uint16_t>(shelf_y_ + shelf_h_ + kPadding);
    shelf_x_ = kPadding;
    shelf_h_ = 0;
  }
  if (shelf_y_ + bm.rows + kPadding > kAtlasSize) {
    throw std::runtime_error{"glyphatlas: out of atlas space"};
  }

  const uint8_t bpp = bytes_per_pixel();
  const size_t row_stride = static_cast<size_t>(kAtlasSize) * bpp;

  if (bm.pixel_mode == FT_PIXEL_MODE_LCD) {
    // 3 bytes/pixel: each input row carries `bm.width` subpixel bytes; we
    // copy them into the atlas at the glyph cell as a packed R/G/B triplet
    // per visual pixel. `bm.pitch` (not bm.width) is the input row stride
    // because FreeType aligns rows to a multiple of 4.
    for (unsigned int row = 0; row < bm.rows; ++row) {
      const uint8_t* src = bm.buffer + static_cast<ptrdiff_t>(row) * bm.pitch;
      uint8_t* dst = bitmap_.data() +
                     static_cast<size_t>(shelf_y_ + row) * row_stride +
                     static_cast<size_t>(shelf_x_) * bpp;
      std::memcpy(dst, src, static_cast<size_t>(visual_w) * 3);
    }
  } else {
    // Grayscale fallback. The atlas may still be either 1- or 3-byte mode:
    //   - 1 bpp: direct copy.
    //   - 3 bpp (atlas is LCD-capable but the LCD load fell back, e.g. on a
    //     bitmap-only font): replicate the gray alpha into all three
    //     subpixel channels so the platform compositor's per-channel blend
    //     still produces the right grayscale result.
    for (unsigned int row = 0; row < bm.rows; ++row) {
      const uint8_t* src = bm.buffer + static_cast<ptrdiff_t>(row) * bm.pitch;
      uint8_t* dst = bitmap_.data() +
                     static_cast<size_t>(shelf_y_ + row) * row_stride +
                     static_cast<size_t>(shelf_x_) * bpp;
      if (bpp == 1) {
        std::memcpy(dst, src, visual_w);
      } else {
        for (unsigned int col = 0; col < visual_w; ++col) {
          const uint8_t a = src[col];
          dst[col * 3 + 0] = a;
          dst[col * 3 + 1] = a;
          dst[col * 3 + 2] = a;
        }
      }
    }
  }
  out.atlas_x = shelf_x_;
  out.atlas_y = shelf_y_;

  mark_dirty(shelf_y_, static_cast<uint16_t>(shelf_y_ + bm.rows));

  shelf_x_ = static_cast<uint16_t>(shelf_x_ + visual_w + kPadding);
  shelf_h_ = std::max<uint16_t>(shelf_h_, static_cast<uint16_t>(bm.rows));
}

void glyphatlas::mark_dirty(uint16_t y0, uint16_t y1) {
  dirty_min_y_ = std::min(dirty_min_y_, y0);
  dirty_max_y_ = std::max(dirty_max_y_, y1);
}

}  // namespace swg