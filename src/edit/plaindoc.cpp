// std
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <generator>
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
  FT_GlyphSlot slot = nullptr;
};

quad make_quad(float x0, float y0, float x1, float y1, float u, float v) {
  // TODO: calculate u,v based on glyph metrics
  return {
      quad_vertex{x0, y0, u, v},
      quad_vertex{x1, y0, u, v},
      quad_vertex{x0, y1, u, v},
      quad_vertex{x1, y1, u, v},
  };
}

size_t mock_quads(quad_vertex* dst, double t) {
  const int cols = 32, rows = 18;
  const float qw = 32.0f, qh = 32.0f;
  const float pad = 8.0f;

  size_t count = 0;
  for (int j = 0; j < rows; ++j) {
    for (int i = 0; i < cols; ++i) {
      float ox = 40.0f + i * (qw + pad);
      float oy = 40.0f + j * (qh + pad);
      float wobble = 6.0f * std::sin((float)t * 2.0f + i * 0.2f + j * 0.3f);
      float x0 = ox, y0 = oy + wobble;
      float x1 = ox + qw, y1 = oy + qh + wobble;

      float u0 = (float)(0.0f + 0.1f * std::sin(t + i * 0.05f));
      float v0 = (float)(0.0f + 0.1f * std::cos(t + j * 0.05f));
      float u1 = u0 + 0.25f, v1 = v0 + 0.25f;

      dst[count * 4 + 0] = {x0, y0, u0, v0};
      dst[count * 4 + 1] = {x1, y0, u1, v0};
      dst[count * 4 + 2] = {x0, y1, u0, v1};
      dst[count * 4 + 3] = {x1, y1, u1, v1};
      ++count;
      if (count >= 512) return count;
    }
  }
  return count;
}

static GLuint mock_atlas() {
  const int W = 128, H = 128;
  std::vector<uint8_t> px(W * H * 4);
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) {
      bool c = ((x / 16) ^ (y / 16)) & 1;
      uint8_t v = c ? 230 : 40;
      px[(y * W + x) * 4 + 0] = v;
      px[(y * W + x) * 4 + 1] = (uint8_t)(x * 2);
      px[(y * W + x) * 4 + 2] = (uint8_t)(y * 2);
      px[(y * W + x) * 4 + 3] = 255;
    }
  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  return tex;
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
    hb_buffer_add_utf8(hbbuf.get(), u8data.data(), (int)u8data.length(), 0, (int)u8data.length());
    hb_buffer_guess_segment_properties(hbbuf.get());
    hb_shape(self->doc->fonts_.front().hbfont(), hbbuf.get(), nullptr, 0);
    unsigned glyph_count = hb_buffer_get_length(hbbuf.get());
    hb_glyph_info_t* glyph_info = hb_buffer_get_glyph_infos(hbbuf.get(), nullptr);
    hb_glyph_position_t* glyph_pos = hb_buffer_get_glyph_positions(hbbuf.get(), nullptr);
    FT_Face face = self->doc->fonts_.front().ftface();
    for (unsigned i = 0; i < glyph_count; ++i) {
      auto& info = glyph_info[i];
      if (FT_Load_Glyph(face, info.codepoint, FT_LOAD_DEFAULT | FT_LOAD_COLOR)) {
        // TODO: log error
        continue;
      }
      if (FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL)) {
        // TODO: log error
        continue;
      }
      co_yield glyph{.info = glyph_info + i, .pos = glyph_pos + i, .slot = face->glyph};
    }
  }
  static std::generator<quad> layout(host* self) {
    float penx = 0, peny = (float)self->doc->fontsize_ * self->dpi / 96.f;
    if (self->doc->ltable_.size() > 0) {
      for (const glyph& g : shape_line(self, 0)) {
        float xoff = g.pos->x_offset / 64.0f;
        float yoff = g.pos->y_offset / 64.0f;
        float xadv = g.pos->x_advance / 64.0f;
        float yadv = g.pos->y_advance / 64.0f;
        float ox = penx + xoff;
        float oy = peny + yoff;
        float x0 = ox + (float)g.slot->bitmap_left;
        float y0 = oy - (float)g.slot->bitmap_top;
        float x1 = x0 + (float)g.slot->bitmap.width;
        float y1 = y0 + (float)g.slot->bitmap.rows;
        co_yield make_quad(x0, y0, x1, y1, 0.0f, 0.0f);
        penx += xadv;
        peny += yadv;
      }
    }
  }
};

static GLint locvp;
static GLint locatlas;
static GLint tex;

void host::initialize_graphics() {
  std::array<float, 9> vertices = {0.0f, 0.5f, -0.5f, -0.5f, 0.5f, -0.5f};
  glprog_ = details::create_gl_program();
  locatlas = glGetUniformLocation(glprog_.get(), "uAtlas");
  locvp = glGetUniformLocation(glprog_.get(), "uViewport");
  tex = mock_atlas();
  streamer_.emplace();
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
  glUniform2f(locvp, (float)viewport.w, (float)viewport.h);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, tex);
  glUniform1i(locatlas, 0);
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
