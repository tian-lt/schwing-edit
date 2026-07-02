// std
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <generator>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>
// icu
#include <unicode/uchar.h>
#include <unicode/uscript.h>
#include <unicode/utf8.h>
// gl
#include <glad/glad.h>
// swg
#include "itemizer.hpp"
#include "plaindoc.hpp"
#include "shaders.hpp"

namespace swg {

namespace {

constexpr int32_t hb_units_per_pixel = 64;

using quad = std::array<quad_vertex, 4>;
struct glyph {
  hb_glyph_info_t* info = nullptr;
  hb_glyph_position_t* pos = nullptr;
  glyphrecord value;
  bool tab = false;
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

hb_position_t to_hb_units(int32_t pixels) { return pixels * hb_units_per_pixel; }

}  // namespace

// ===--------------------
// plaindoc implementation
plaindoc::plaindoc(double fontsize, eol eol, std::optional<std::filesystem::path> filepath)
    : ptable_(filepath ? piecetable::from_file(*filepath) : piecetable{}),
      ltable_(eol),
      hbbuf_(hb_buffer_create()),
      fontsize_(fontsize),
      eol_(eol) {
  if (filepath) {
    mixeol_ = ltable_.rebuild(ptable_, eol);
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
  static int line_height(const host* self) {
    return (int)std::ceil((float)self->doc->fontsize_ * self->dpi / 96.f * 1.5f);
  }
  static int line_count(const host* self) { return self->doc ? (int)self->doc->ltable_.size() : 0; }
  static int page_lines(const host* self) {
    if (self->doc == nullptr) {
      return 1;
    }
    int lh = line_height(self);
    return lh > 0 ? std::max(1, self->height_ / lh) : 1;
  }
  static int max_topline(const host* self) {
    int total = line_count(self);
    if (total <= 1) {
      return 0;  // a single line never scrolls
    }
    return total - 1;  // scroll-beyond-last-line: the last line can reach the top
  }
  static void notify_vscroll(host* self) {
    self->topline_ = std::clamp(self->topline_, 0, max_topline(self));
    self->on_vscroll({.total_lines = line_count(self),
                      .page_lines = page_lines(self),
                      .top_line = self->topline_,
                      .max_top_line = max_topline(self)});
  }
  static void ensure_caret_visible(host* self) {
    if (self->doc == nullptr) {
      return;
    }
    int line = (int)self->doc->ltable_.line_at_pos(self->inspos_);
    int page = page_lines(self);
    if (line < self->topline_) {
      self->topline_ = line;
    } else if (line > self->topline_ + page - 1) {
      self->topline_ = line - page + 1;
    }
  }
  static void post_caret_move(host* self) {
    ensure_caret_visible(self);
    notify_vscroll(self);
    self->on_invalidate();
  }
  static void post_edit(host* self, size_t /*pos_before*/, size_t /*pos_after*/) {
    post_caret_move(self);
  }
  // docpos.column counts UTF-8 codepoints from the line start (excluding the eol).
  static docpos caret_docpos(const host* self) {
    if (self->doc == nullptr) {
      return {.line = 0, .column = 0};
    }
    size_t pos = self->inspos_;
    int li = (int)self->doc->ltable_.line_at_pos(pos);
    size_t beg = self->doc->ltable_[(size_t)li].beg;
    int col = 0;
    std::array<char, 256> buf;
    for (size_t off = beg; off < pos;) {
      size_t want = std::min(buf.size(), pos - off);
      self->doc->ptable_.get_to(off, std::span<char>{buf.data(), want});
      for (size_t i = 0; i < want; ++i) {
        if (((unsigned char)buf[i] & 0xC0) != 0x80) {
          ++col;
        }
      }
      off += want;
    }
    return {.line = li, .column = col};
  }
  static size_t line_content_bytes(const host* self, size_t beg, size_t length) {
    size_t tail = std::min<size_t>(2, length);
    if (tail == 0) {
      return 0;
    }
    std::string end = self->doc->get(beg + length - tail, tail);
    if (end.back() == '\n') {
      return length - (end.size() >= 2 && end[end.size() - 2] == '\r' ? 2 : 1);
    }
    if (end.back() == '\r') {
      return length - 1;
    }
    return length;
  }
  static void set_caret(host* self, docpos pos) {
    if (self->doc == nullptr) {
      return;
    }
    int total = line_count(self);
    if (total == 0) {
      self->inspos_ = 0;
      post_caret_move(self);
      return;
    }
    int li = std::clamp(pos.line, 0, total - 1);
    auto line = self->doc->ltable_[(size_t)li];
    size_t content = line_content_bytes(self, line.beg, line.length);
    size_t off = 0;
    if (pos.column > 0 && (size_t)pos.column >= content) {
      off = content;  // a line has no more codepoints than bytes, so this is the eol
    } else if (pos.column > 0) {
      // Walk codepoints in chunks, stopping as soon as the column is reached.
      int col = 0;
      std::array<char, 256> buf;
      while (off < content && col < pos.column) {
        size_t want = std::min(buf.size(), content - off);
        self->doc->ptable_.get_to(line.beg + off, std::span<char>{buf.data(), want});
        size_t i = 0;
        while (i < want && col < pos.column) {
          size_t adv = 1;
          while (i + adv < want && ((unsigned char)buf[i + adv] & 0xC0) == 0x80) {
            ++adv;
          }
          if (i + adv == want && off + want < content) {
            break;  // codepoint may span the chunk boundary; re-read it next round
          }
          i += adv;
          ++col;
        }
        off += i;
      }
    }
    self->inspos_ = line.beg + off;
    post_caret_move(self);
  }
  static void reset_graphics(host* self) {
    self->streamer_.emplace();
    self->atlas_.emplace(512, 512);
  }
  static fontengine& select_font(host* self, UScriptCode script) {
    auto& fonts = self->doc->fonts_;
    auto it = fonts.find(script);
    if (it == fonts.end()) {
      std::string path = font_for_script(script);
      it = fonts.try_emplace(script, path, self->doc->fontsize_, self->dpi).first;
    }
    return it->second;
  }
  static std::generator<glyph> shape_line(host* self, size_t line_idx) {
    auto line = self->doc->ltable_[line_idx];
    if (line.length == 0) {
      co_return;
    }
    const piecetable& ptable = self->doc->ptable_;
    constexpr size_t block_bytes = 4096;
    constexpr size_t chunk_bytes = 256;
    std::string u8data;
    size_t read = 0;               // bytes pulled from the piecetable so far
    size_t content = line.length;  // shrinks to drop the trailing eol once the tail is read
    std::vector<scriptrun> runs;
    itemizer itz{[&](scriptrun run) { runs.push_back(run); }};
    auto hbbuf = self->doc->hbbuf_.get();
    scope_guard guard{[hbbuf] { hb_buffer_reset(hbbuf); }};
    auto is_separator = [](char c) { return c == '\t' || c == '\r' || c == '\n'; };
    for (size_t base = 0; base < content;) {
      size_t want = std::min(base + chunk_bytes, line.length);
      while (read < want) {
        size_t n = std::min(block_bytes, line.length - read);
        size_t off = u8data.size();
        u8data.resize(off + n);
        ptable.get_to(line.beg + read, std::span<char>{u8data.data() + off, n});
        read += n;
      }
      if (read == line.length) {
        while (!u8data.empty() && (u8data.back() == '\r' || u8data.back() == '\n')) {
          u8data.pop_back();
        }
        content = u8data.size();
      }
      size_t end = std::min(base + chunk_bytes, content);
      bool separated = false;
      for (size_t i = base; i < end; ++i) {
        if (is_separator(u8data[i])) {
          end = i + 1;
          separated = true;
          break;
        }
      }
      runs.clear();
      itz.feed(std::string_view{u8data}.substr(base, end - base));
      if (separated || end == content) {
        itz.finish();
      } else {
        itz.flush();
      }
      base = end;
      for (const scriptrun& run : runs) {
        fontengine& font = select_font(self, run.script_code);
        hb_buffer_clear_contents(hbbuf);
        hb_buffer_add_utf8(hbbuf, u8data.data(), (int)u8data.length(), (unsigned)run.byte_offset,
                           (int)run.byte_length);
        if (const char* tag = uscript_getShortName(run.script_code)) {
          hb_script_t script = hb_script_from_string(tag, -1);
          if (script != HB_SCRIPT_INVALID) {
            hb_buffer_set_script(hbbuf, script);
          }
        }
        hb_buffer_guess_segment_properties(hbbuf);
        hb_shape(font.hbfont(), hbbuf, nullptr, 0);
        unsigned glyph_count = hb_buffer_get_length(hbbuf);
        hb_glyph_info_t* glyph_info = hb_buffer_get_glyph_infos(hbbuf, nullptr);
        hb_glyph_position_t* glyph_pos = hb_buffer_get_glyph_positions(hbbuf, nullptr);
        FT_Face face = font.ftface();
        for (unsigned i = 0; i < glyph_count; ++i) {
          auto& info = glyph_info[i];
          UChar32 cp = -1;
          if (info.cluster < u8data.size()) {
            int32_t _ = (int32_t)info.cluster;
            U8_NEXT(u8data.data(), _, (int32_t)u8data.size(), cp);
          }
          if (cp == '\t') {
            co_yield glyph{.info = glyph_info + i, .pos = glyph_pos + i, .tab = true};
            continue;
          }
          if (cp >= 0 && u_isUWhiteSpace(cp)) {
            co_yield glyph{.info = glyph_info + i, .pos = glyph_pos + i};
            continue;
          }
          auto g = self->atlas_->try_get(face, info.codepoint);
          if (!g.has_value()) {
            if (FT_Load_Glyph(face, info.codepoint, FT_LOAD_FORCE_AUTOHINT)) {
              continue;
            }
            if (FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL)) {
              continue;
            }
            g.emplace(self->atlas_->set(face, info.codepoint));
          }
          co_yield glyph{.info = glyph_info + i, .pos = glyph_pos + i, .value = *g};
        }
      }
    }
  }
  static std::generator<quad> layout(host* self, int width, int height) {
    constexpr hb_position_t origin = hb_units_per_pixel;
    hb_position_t space_adv = select_font(self, USCRIPT_LATIN).space_advance();
    hb_position_t tab_unit = space_adv * self->tabsize;
    hb_position_t width_limit = to_hb_units(width);
    hb_position_t height_limit = to_hb_units(height);
    hb_position_t peny = 0;
    for (size_t l = (size_t)self->topline_; l < self->doc->ltable_.size(); ++l) {
      if (peny > height_limit) {
        break;
      }
      hb_position_t penx = origin;
      peny += to_hb_units(line_height(self));
      for (const glyph& g : shape_line(self, l)) {
        if (penx > width_limit) {
          break;
        }
        if (g.tab) {
          if (tab_unit > 0) {
            hb_position_t steps = (penx - origin) / tab_unit + 1;
            penx = origin + steps * tab_unit;
          }
          continue;
        }
        int32_t x0 = penx + g.pos->x_offset + to_hb_units(g.value.ext.left);
        int32_t y0 = peny + g.pos->y_offset - to_hb_units(g.value.ext.top);
        int32_t x1 = x0 + to_hb_units(g.value.uv.w);
        int32_t y1 = y0 + to_hb_units(g.value.uv.h);
        penx += g.pos->x_advance;
        peny += g.pos->y_advance;
        if (x1 - x0 == 0 && y1 - y0 == 0) {
          continue;
        }
        co_yield make_quad(x0, y0, x1, y1, g.value.uv);
      }
    }
  }
};

void host::initialize_graphics() {
  glprog_ = details::create_gl_program();
  glUseProgram(glprog_.get());
  loc_viewport_ = glGetUniformLocation(glprog_.get(), "uViewport");
}
void host::set(plaindoc* new_doc) {
  if (doc == new_doc) {
    return;
  }
  doc = new_doc;
  inspos_ = 0;
  topline_ = 0;
  impl::reset_graphics(this);
  impl::notify_vscroll(this);
  on_invalidate();
}
void host::resize(int width, int height) {
  width_ = width;
  height_ = height;
  impl::notify_vscroll(this);
  on_invalidate();
}
int host::line_count() const { return impl::line_count(this); }
int host::page_lines() const { return impl::page_lines(this); }
void host::scroll_to_line(int line) {
  int clamped = std::clamp(line, 0, impl::max_topline(this));
  if (clamped == topline_) {
    return;
  }
  topline_ = clamped;
  impl::notify_vscroll(this);
  on_invalidate();
}
docpos host::caret() const { return impl::caret_docpos(this); }
void host::caret(docpos pos) { impl::set_caret(this, pos); }
void host::render() {
  if (doc == nullptr || width_ <= 0 || height_ <= 0) {
    return;
  }
  glViewport(0, 0, width_, height_);
  glUseProgram(glprog_.get());
  glUniform2i(loc_viewport_, width_, height_);
  auto quadgen = impl::layout(this, width_, height_);
  auto qiter = quadgen.begin();
  while (qiter != quadgen.end()) {
    quad_vertex* verts = streamer_->begin();
    size_t quad_count = 0;
    for (; qiter != quadgen.end() && quad_count < glstreamer::max_quads_per_frame;
         ++qiter, ++quad_count) {
      std::memcpy(verts, (*qiter).data(), sizeof(quad));
      verts += (*qiter).size();
    }
    atlas_->try_bind_gl(glprog_);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    streamer_->end(quad_count);
  }
}
void host::insert_char(std::string_view u8char) {
  assert(u8char != "\r" && u8char != "\n" && u8char != "\b");
  if (doc == nullptr) {
    return;
  }
  size_t before = inspos_;
  doc->insert(inspos_, u8char);
  inspos_ += u8char.length();
  impl::post_edit(this, before, inspos_);
}
void host::erase_char() {
  if (doc == nullptr || doc->length() == 0 || inspos_ == 0) {
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
  if (doc == nullptr) {
    return;
  }
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
