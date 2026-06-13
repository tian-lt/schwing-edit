// std
#include <optional>
#include <utility>
// swg
#include "glyphatlas.hpp"

namespace swg {

struct glyphatlas::impl {
  static void add_page(glyphatlas* self, unsigned w, unsigned h) {
    size_t old_page_count = self->pages_.size();
    self->pages_.emplace_back();
    auto old = std::exchange(self->texarr_, {});
    glGenTextures(1, self->texarr_.put());
    glBindTexture(GL_TEXTURE_2D_ARRAY, self->texarr_.get());
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_R8, w, h, (GLint)self->pages_.size(), 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (old) {
      unique_gl_framebuffer fbo;
      glGenFramebuffers(1, fbo.put());
      glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo.get());
      for (size_t i = 0; i < old_page_count; ++i) {
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

glyphatlas::glyphatlas(unsigned width, unsigned height) : width_(width), height_(height) {
  impl::add_page(this, width_, height_);
}

}  // namespace swg