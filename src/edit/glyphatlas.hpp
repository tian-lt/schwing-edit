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
  float layer;
};
struct glyphext {
  float left;
  float top;
};
struct glyphrecord {
  glyphuv uv;
  glyphext ext;
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
  std::optional<glyphrecord> try_get(FT_Face face, hb_codepoint_t codepoint) const;
  glyphrecord set(FT_Face face, hb_codepoint_t codepoint);
  bool try_bind_gl(const unique_gl_program& program) const;

 private:
  unique_gl_texture texarr_;
  shelfset<shelfkey, glyphext, shelfkey_hash> shelves_;
  unsigned width_ = 0;
  unsigned height_ = 0;
};

}  // namespace swg
