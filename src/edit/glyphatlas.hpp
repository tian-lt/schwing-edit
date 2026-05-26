#pragma once
// std
#include <cstdint>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>
// swg
#include "resource.hpp"

namespace swg {

struct glyph_info {
  uint16_t atlas_x = 0;    // px, top-left of glyph cell in the atlas
  uint16_t atlas_y = 0;
  uint16_t width = 0;      // px
  uint16_t height = 0;     // px
  int16_t bearing_x = 0;   // px, glyph left side bearing (FT bitmap_left)
  int16_t bearing_y = 0;   // px, glyph top above baseline (FT bitmap_top)
  int32_t advance_26_6 = 0;  // 26.6 fixed-point horizontal advance from FT slot
};

// CPU-side glyph cache + shelf-packed atlas. Pure data — no GPU.
// The platform host reads the bitmap via `bitmap()` to render via its
// preferred graphics API.
//
// Pixel format depends on the rendering mode the atlas was constructed with:
//   - `grayscale`: 1 byte/pixel; the platform composes by alpha-blending the
//     foreground color uniformly across all subpixels.
//   - `lcd_rgb`: 3 bytes/pixel (B,G,R packed contiguously like a 24-bit
//     bitmap — see `bytes_per_pixel()`), allowing per-subpixel ClearType-
//     style filtering when the platform writes the BGRA framebuffer. The
//     atlas internally stores the bytes in R,G,B order; the consumer should
//     blend each component independently against the matching destination
//     subpixel.
//
// The constructor picks `lcd_rgb` automatically when FreeType reports LCD
// filtering support (see `swg::ft_lcd_filter_available()`); otherwise the
// atlas degrades gracefully to grayscale. Pass `force_grayscale=true` to
// pin the atlas to grayscale (handy for tests).
class glyphatlas {
 public:
  static constexpr uint16_t kAtlasSize = 1024;
  static constexpr uint16_t kPadding = 1;  // 1 px gutter between glyphs

  enum struct render_mode : uint8_t {
    grayscale = 1,  // 1 byte/pixel
    lcd_rgb = 3,    // 3 bytes/pixel (R, G, B) — full ClearType subpixel
  };

  explicit glyphatlas(FT_Face face, bool force_grayscale = false);

  // Return cached glyph metrics; rasterizes the glyph on first request.
  // Throws std::runtime_error on FT load failure or atlas overflow.
  const glyph_info& get(uint32_t glyph_id);

  std::span<const uint8_t> bitmap() const { return bitmap_; }
  uint16_t width() const { return kAtlasSize; }
  uint16_t height() const { return kAtlasSize; }
  // Bytes per atlas pixel — 1 for grayscale, 3 for LCD subpixel. Used by the
  // platform compositor to step through the bitmap and decide how to blend
  // against the framebuffer.
  uint8_t bytes_per_pixel() const { return static_cast<uint8_t>(mode_); }
  render_mode mode() const { return mode_; }

  // Range of bitmap rows that have changed since the last clear_dirty().
  bool dirty() const { return dirty_min_y_ < dirty_max_y_; }
  std::pair<uint16_t, uint16_t> dirty_rows() const { return {dirty_min_y_, dirty_max_y_}; }
  void clear_dirty() {
    dirty_min_y_ = kAtlasSize;
    dirty_max_y_ = 0;
  }

  // Number of glyphs currently cached. Exposed for tests.
  size_t cached_count() const { return cache_.size(); }

 private:
  void rasterize_into(uint32_t glyph_id, glyph_info& out);
  void mark_dirty(uint16_t y0, uint16_t y1);

  FT_Face face_;
  render_mode mode_ = render_mode::grayscale;
  std::vector<uint8_t> bitmap_;
  std::unordered_map<uint32_t, glyph_info> cache_;
  uint16_t shelf_x_ = kPadding;
  uint16_t shelf_y_ = kPadding;
  uint16_t shelf_h_ = 0;
  uint16_t dirty_min_y_ = kAtlasSize;
  uint16_t dirty_max_y_ = 0;
};

}  // namespace swg