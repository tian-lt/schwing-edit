// std
#include <algorithm>
#include <array>
#include <cassert>
// gl
#include <glad/glad.h>
// swg
#include "plaindoc.hpp"
#include "shaders.hpp"

namespace swg {

// ===--------------------
// plaindoc implementation
plaindoc::plaindoc(host* host, std::string fontpath, double fontsize, eol eol)
    : ltable_(eol), host_(host), fontpath_(fontpath), fontsize_(fontsize), eol_(eol) {
  fonts_.emplace_back(fontpath_, fontsize_);
}
void plaindoc::reset(std::optional<std::string> new_fontpath, std::optional<eol> new_eol) {
  if (new_fontpath.has_value()) {
    fontpath_ = *new_fontpath;
  }
  if (new_eol.has_value()) {
    mixeol_ = ltable_.rebuild(ptable_, *new_eol);
  }
}
void plaindoc::insert(size_t pos, std::string_view data) {
  ptable_.insert(pos, data);
  ltable_.insert(pos, data);
}
void plaindoc::erase(size_t pos, size_t length) {
  ptable_.erase(pos, length);
  ltable_.erase(pos, length);
}

// ===----------------
// host implementation
struct host::impl {
  static void post_edit(host* self, size_t pos_before, size_t pos_after) {
    auto l0 = self->doc->ltable_.line_at_pos(pos_before);
    auto l1 = self->doc->ltable_.line_at_pos(pos_after);
    l0 = l0 > l1 ? l1 : l0;
    // TODO: update underlying data
    self->on_invalidate({});
  }
  static void shape_line(host* self, size_t line_idx) {
    auto line = self->doc->ltable_[line_idx];
    unique_hb_buffer hbbuf{hb_buffer_create()};  // TODO: reuse buffers
    auto u8data = self->doc->ptable_.get(line.beg, line.length);
    hb_buffer_add_utf8(hbbuf.get(), u8data.data(), (int)u8data.length(), 0, (int)u8data.length());
    hb_buffer_guess_segment_properties(hbbuf.get());
    hb_shape(self->doc->fonts_.front().hbfont(), hbbuf.get(), nullptr, 0);
    unsigned glyph_count = hb_buffer_get_length(hbbuf.get());
    hb_glyph_info_t* glyph_info = hb_buffer_get_glyph_infos(hbbuf.get(), nullptr);
    hb_glyph_position_t* glyph_pos = hb_buffer_get_glyph_positions(hbbuf.get(), nullptr);
    FT_Face face = self->doc->fonts_.front().ftface();
    for (unsigned i = 0; i < glyph_count; ++i) {
      auto& info = glyph_info[i];
      auto& pos = glyph_pos[i];
      (void)pos;
      if (FT_Load_Glyph(face, info.codepoint, FT_LOAD_DEFAULT)) {
        // TODO: log error
        continue;
      }
      if (FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL)) {
        // TODO: log error
        continue;
      }
    }
  }
};

void host::initialize_graphics() {
  std::array<float, 9> vertices = {0.0f, 0.5f, 0.0f, -0.5f, -0.5f, 0.0f, 0.5f, -0.5f, 0.0f};
  glGenVertexArrays(1, vao_.put());
  glGenBuffers(1, vbo_.put());
  glBindVertexArray(vao_.get());
  glBindBuffer(GL_ARRAY_BUFFER, vbo_.get());
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices.data(), GL_STATIC_DRAW);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
  glEnableVertexAttribArray(0);
  glprog_ = details::create_gl_program();
}
void host::render(rect /*rc*/) {
  if (doc->ltable_.size() > 0) {
    impl::shape_line(this, 0);
  }
  glUseProgram(glprog_.get());
  glBindVertexArray(vao_.get());
  glDrawArrays(GL_TRIANGLES, 0, 3);
}
void host::insert_char(std::string_view u8char) {
  assert(u8char != "\r" && u8char != "\n" && u8char != "\b");
  size_t before = inspos_;
  doc->insert(inspos_, u8char);
  inspos_ += u8char.length();
  impl::post_edit(this, before, inspos_);
}
void host::erase_char() {
  if (doc->length() == 0 || inspos_ == 0) {
    return;
  }
  size_t l = std::min(6uz, inspos_);
  auto s = doc->get(inspos_ - l, l);
  size_t e = inspos_ - 1;
  for (auto it = s.rbegin(); it != s.rend(); ++it) {
    if ((*it & 0xC0) != 0x80) {
      break;
    }
    --e;
  }
  doc->erase(e, inspos_ - e);
  size_t before = inspos_;
  inspos_ = e;
  impl::post_edit(this, before, inspos_);
}
void host::linefeed() {
  size_t before = inspos_;
  switch (doc->eol_) {
    case eol::cr:
      doc->insert(inspos_, "\r");
      ++inspos_;
      break;
    case eol::crlf:
      doc->insert(inspos_, "\r\n");
      inspos_ += 2;
      break;
    case eol::lf:
      doc->insert(inspos_, "\n");
      ++inspos_;
      break;
    default:
      std::unreachable();
  }
  impl::post_edit(this, before, inspos_);
}

}  // namespace swg
