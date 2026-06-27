// std
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <generator>
#include <ranges>
#include <string>
#include <string_view>
// gl
#include <glad/glad.h>
// swg
#include "plaindoc.hpp"
#include "shaders.hpp"

namespace swg {

namespace {

using quad = std::array<quad_vertex, 4>;
struct glyph {
  hb_glyph_info_t* info = nullptr;
  hb_glyph_position_t* pos = nullptr;
  glyphrecord value;
};

quad make_quad(int32_t x0, int32_t y0, int32_t x1, int32_t y1, const glyphuv& uv) {
  int32_t u0 = uv.u, v0 = uv.v;
  int32_t u1 = uv.u + uv.w, v1 = uv.v + uv.h;
  int32_t layer = uv.layer;
  return {
      quad_vertex{x0, y0, u0, v0, layer},
      quad_vertex{x1, y0, u1, v0, layer},
      quad_vertex{x0, y1, u0, v1, layer},
      quad_vertex{x1, y1, u1, v1, layer},
  };
}

}  // namespace

// ===--------------------
// plaindoc implementation
plaindoc::plaindoc(host* host, std::string fontpath, double fontsize, eol eol)
    : ltable_(eol), host_(host), fontpath_(fontpath), fontsize_(fontsize), eol_(eol) {}
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
  static std::generator<glyph> shape_line(host* self, size_t line_idx) {
    auto line = self->doc->ltable_[line_idx];
    unique_hb_buffer hbbuf{hb_buffer_create()};  // TODO: reuse buffers
    auto u8data = self->doc->ptable_.get(line.beg, line.length);
    auto eolCount =
        std::ranges::distance(u8data | std::views::reverse | std::views::take_while([](char ch) {
                                return ch == '\r' || ch == '\n';
                              }));
    u8data.resize(u8data.size() - eolCount);
    if (u8data.empty()) {
      co_return;
    }
    hb_buffer_add_utf8(hbbuf.get(), u8data.data(), (int)u8data.length(), 0, (int)u8data.length());
    hb_buffer_guess_segment_properties(hbbuf.get());
    hb_shape(self->doc->fonts_.front().hbfont(), hbbuf.get(), nullptr, 0);
    unsigned glyph_count = hb_buffer_get_length(hbbuf.get());
    hb_glyph_info_t* glyph_info = hb_buffer_get_glyph_infos(hbbuf.get(), nullptr);
    hb_glyph_position_t* glyph_pos = hb_buffer_get_glyph_positions(hbbuf.get(), nullptr);
    FT_Face face = self->doc->fonts_.front().ftface();
    for (unsigned i = 0; i < glyph_count; ++i) {
      auto& info = glyph_info[i];
      auto g = self->atlas_->try_get(face, info.codepoint);
      if (!g.has_value()) {
        if (FT_Load_Glyph(face, info.codepoint, FT_LOAD_DEFAULT)) {
          // TODO: log error
          continue;
        }
        if (FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL)) {
          // TODO: log error
          continue;
        }
        g.emplace(self->atlas_->set(face, info.codepoint));
      }
      co_yield glyph{.info = glyph_info + i, .pos = glyph_pos + i, .value = *g};
    }
  }
  static std::generator<quad> layout(host* self) {
    float peny = 0.f;
    for (size_t l = 0; l < self->doc->ltable_.size(); ++l) {
      float penx = 1.f;
      peny += ((float)self->doc->fontsize_ * self->dpi / 96.f) * 1.5f;
      for (const glyph& g : shape_line(self, l)) {
        float xoff = g.pos->x_offset / 64.0f;
        float yoff = g.pos->y_offset / 64.0f;
        float xadv = g.pos->x_advance / 64.0f;
        float yadv = g.pos->y_advance / 64.0f;
        float ox = penx + xoff;
        float oy = peny + yoff;
        int32_t x0 = std::lround(ox + g.value.ext.left);
        int32_t y0 = std::lround(oy - g.value.ext.top);
        int32_t x1 = x0 + g.value.uv.w;
        int32_t y1 = y0 + g.value.uv.h;
        penx += xadv;
        peny += yadv;
        if (x1 - x0 == 0 && y1 - y0 == 0) {
          continue;
        }
        co_yield make_quad(x0, y0, x1, y1, g.value.uv);
      }
    }
  }
};

void host::initialize_graphics() {
  std::array<float, 9> vertices = {0.0f, 0.5f, -0.5f, -0.5f, 0.5f, -0.5f};
  glprog_ = details::create_gl_program();
  loc_viewport_ = glGetUniformLocation(glprog_.get(), "uViewport");
  streamer_.emplace();
  atlas_.emplace(512, 512);
  doc->fonts_.emplace_back(doc->fontpath_, doc->fontsize_, dpi);
}

void host::render(rect /*rc*/) {
  quad_vertex* verts = streamer_->begin();
  size_t quad_count = 0;
  auto quadgen = impl::layout(this);
  for (const auto& q : quadgen) {
    std::memcpy(verts, q.data(), sizeof(quad));
    verts += q.size();
    ++quad_count;
  }
  glUseProgram(glprog_.get());
  glUniform2f(loc_viewport_, (float)viewport.w, (float)viewport.h);
  atlas_->try_bind_gl(glprog_);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  streamer_->end(quad_count);
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
  if (s.size() >= 2 && s.back() == '\n' && s[s.size() - 2] == '\r') {
    --e;
  } else {
    for (auto it = s.rbegin(); it != s.rend(); ++it) {
      if ((*it & 0xC0) != 0x80) {
        break;
      }
      --e;
    }
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
