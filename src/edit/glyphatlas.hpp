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

// CPU-side glyph cache + shelf-packed single-channel atlas. Pure data — no GPU.
// The platform host reads the bitmap via `bitmap()` to render via its preferred
// graphics API.
class glyphatlas {
 public:
  static constexpr uint16_t kAtlasSize = 1024;
  static constexpr uint16_t kPadding = 1;  // 1 px gutter between glyphs

  explicit glyphatlas(FT_Face face);

  // Return cached glyph metrics; rasterizes the glyph on first request.
  // Throws std::runtime_error on FT load failure or atlas overflow.
  const glyph_info& get(uint32_t glyph_id);

  std::span<const uint8_t> bitmap() const { return bitmap_; }
  uint16_t width() const { return kAtlasSize; }
  uint16_t height() const { return kAtlasSize; }

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
  std::vector<uint8_t> bitmap_;
  std::unordered_map<uint32_t, glyph_info> cache_;
  uint16_t shelf_x_ = kPadding;
  uint16_t shelf_y_ = kPadding;
  uint16_t shelf_h_ = 0;
  uint16_t dirty_min_y_ = kAtlasSize;
  uint16_t dirty_max_y_ = 0;
};

}  // namespace swg