#pragma once

// std
#include <optional>
#include <vector>
// swg
#include "resource.hpp"
#include "shelf.hpp"

namespace swg {

struct glyphuv {
  float u;
  float v;
  float w;
  float h;
};

class glyphatlas {
  struct impl;
  struct shelfkey {
    FT_Face face;
    hb_codepoint_t codepoint;
    friend auto operator<=>(const shelfkey&, const shelfkey&) = default;
  };
  struct shelfkey_hash {
    static size_t operator()(const shelfkey& key) noexcept;
  };

 public:
  explicit glyphatlas(unsigned width, unsigned height);
  std::optional<glyphuv> try_get(FT_Face face, hb_codepoint_t codepoint) const;
  glyphuv set(FT_Face face, hb_codepoint_t codepoint);

 private:
  unique_gl_texture texarr_;
  shelfset<shelfkey, int, shelfkey_hash> shelves_;
  unsigned width_ = 0;
  unsigned height_ = 0;
};

}  // namespace swg
