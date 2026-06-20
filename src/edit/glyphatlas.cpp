// std
#include <optional>
#include <utility>
// swg
#include "glyphatlas.hpp"

namespace swg {

struct glyphatlas::impl {
  static void add_page(glyphatlas* self, unsigned w, unsigned h, size_t old_size) {
    auto old = std::exchange(self->texarr_, {});
    glGenTextures(1, self->texarr_.put());
    glBindTexture(GL_TEXTURE_2D_ARRAY, self->texarr_.get());
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_R8, w, h, (GLint)old_size + 1, 0, GL_RED,
                 GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (old) {
      unique_gl_framebuffer fbo;
      glGenFramebuffers(1, fbo.put());
      glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo.get());
      for (size_t i = 0; i < old_size; ++i) {
        glFramebufferTextureLayer(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, old.get(), 0,
                                  (GLint)i);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glCheckFramebufferStatus(GL_READ_FRAMEBUFFER);
        glBindTexture(GL_TEXTURE_2D_ARRAY, self->texarr_.get());
        glCopyTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, (GLint)i, 0, 0, w, h);
      }
      glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    }
  };
};

size_t glyphatlas::shelfkey_hash::operator()(const shelfkey& key) noexcept {
  size_t h1 = std::hash<FT_Face>::operator()(key.face);
  size_t h2 = std::hash<hb_codepoint_t>::operator()(key.codepoint);
  return h1 ^ (h2 << 1);
}

glyphatlas::glyphatlas(unsigned width, unsigned height)
    : shelves_(width, height), width_(width), height_(height) {}

std::optional<glyphrecord> glyphatlas::try_get(FT_Face face, hb_codepoint_t codepoint) const {
  auto* cell = shelves_.try_get_view({face, codepoint});
  if (cell) {
    return glyphrecord{.uv = {.u = (int32_t)cell->x,
                              .v = (int32_t)cell->y,
                              .w = (int32_t)cell->w,
                              .h = (int32_t)cell->h,
                              .layer = (int32_t)cell->shelf_index},
                       .ext = {.left = cell->payload.left, .top = cell->payload.top}};
  }
  return std::nullopt;
}

bool glyphatlas::try_bind_gl(const unique_gl_program& program) const {
  if (!texarr_.has_value()) {
    return false;
  }
  GLint loc_atlas = glGetUniformLocation(program.get(), "uAtlas");
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D_ARRAY, texarr_.get());
  glUniform1i(loc_atlas, 0);
  GLint loc_size = glGetUniformLocation(program.get(), "uAtlasSize");
  glUniform2f(loc_size, (float)width_, (float)height_);
  return true;
}

glyphrecord glyphatlas::set(FT_Face face, hb_codepoint_t codepoint) {
  const shelfcell<glyphext>* cell;
  glyphext ext{.left = (int32_t)face->glyph->bitmap_left, .top = (int32_t)face->glyph->bitmap_top};
  if (cell = shelves_.try_put({face, codepoint}, ext, face->glyph->bitmap.width,
                              face->glyph->bitmap.rows);
      !cell) {
    impl::add_page(this, width_, height_, shelves_.shelf_count());
    shelves_.add_shelf();
    cell = shelves_.try_put({face, codepoint}, ext, face->glyph->bitmap.width,
                            face->glyph->bitmap.rows);
  }
  assert(cell);
  glyphuv g{.u = (int32_t)cell->x,
            .v = (int32_t)cell->y,
            .w = (int32_t)cell->w,
            .h = (int32_t)cell->h,
            .layer = (int32_t)cell->shelf_index};
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glBindTexture(GL_TEXTURE_2D_ARRAY, texarr_.get());
  glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, g.u, g.v, g.layer, g.w, g.h, 1, GL_RED, GL_UNSIGNED_BYTE,
                  face->glyph->bitmap.buffer);
  return glyphrecord{.uv = g, .ext = ext};
}

}  // namespace swg