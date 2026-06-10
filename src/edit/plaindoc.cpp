// std
#include <algorithm>
#include <cstdint>
#include <utility>
// swg
#include "plaindoc.hpp"

namespace swg {

struct plaindoc::impl {
  static bool is_utf8_continuation(unsigned char b) noexcept { return (b & 0xC0) == 0x80; }
  static bool is_word_char(unsigned char b) noexcept {
    // ASCII word = letters/digits/underscore; UTF-8 continuation bytes count as
    // part of a word (extended chars).
    if (b >= 0x80) return true;
    return (b >= '0' && b <= '9') || (b >= 'A' && b <= 'Z') || (b >= 'a' && b <= 'z') ||
           b == '_';
  }

  static std::size_t step_left_codepoint(const plaindoc* self, std::size_t pos) {
    if (pos == 0) return 0;
    std::size_t p = pos - 1;
    while (p > 0) {
      const auto b = self->pt_.get(p, 1);
      if (b.empty() || !is_utf8_continuation(static_cast<unsigned char>(b[0]))) break;
      --p;
    }
    return p;
  }
  static std::size_t step_right_codepoint(const plaindoc* self, std::size_t pos) {
    const std::size_t len = self->pt_.length();
    if (pos >= len) return len;
    std::size_t p = pos + 1;
    while (p < len) {
      const auto b = self->pt_.get(p, 1);
      if (b.empty() || !is_utf8_continuation(static_cast<unsigned char>(b[0]))) break;
      ++p;
    }
    return p;
  }

  static void delete_selection(plaindoc* self) {
    if (!self->has_selection()) return;
    const auto [lo, hi] = self->selection_range();
    const std::size_t n = hi - lo;
    self->pt_.erase(lo, n);
    self->lt_.erase(self->pt_, lo, n);
    self->caret_ = lo;
    self->anchor_ = lo;
    if (self->host_) {
      self->host_->on_doc_changed(damage{.pos = lo, .erased_len = n, .inserted_len = 0});
    }
  }

  static void notify_changed(plaindoc* self, std::size_t pos, std::size_t erased,
                             std::size_t inserted) {
    if (self->host_) {
      self->host_->on_doc_changed(damage{.pos = pos, .erased_len = erased, .inserted_len = inserted});
    }
  }

  static void notify_caret(plaindoc* self) {
    if (self->host_) self->host_->on_caret_moved();
  }
};

plaindoc::plaindoc() { lt_.rebuild(pt_); }

void plaindoc::load_text(std::string utf8) {
  // We need owned storage; copy into addbuf via a fresh piecetable.
  pt_.reset({});                 // empty initbuf
  pt_.insert(0, utf8);           // copies into addbuf
  mixed_eol_ = lt_.rebuild(pt_);
  eol_ = lt_.dominant_eol();
  caret_ = anchor_ = 0;
  pref_col_valid_ = false;
  if (host_) host_->on_invalidate();
}

void plaindoc::load_view(std::string_view utf8) {
  pt_.reset(utf8);  // initbuf aliases external storage
  mixed_eol_ = lt_.rebuild(pt_);
  eol_ = lt_.dominant_eol();
  caret_ = anchor_ = 0;
  pref_col_valid_ = false;
  if (host_) host_->on_invalidate();
}

void plaindoc::materialize() { pt_.materialize(); }

std::size_t plaindoc::snap_to_codepoint_left(std::size_t pos) const {
  if (pos == 0) return 0;
  const std::size_t len = pt_.length();
  if (pos >= len) return len;
  std::size_t p = pos;
  while (p > 0) {
    const auto b = pt_.get(p, 1);
    if (b.empty() || !impl::is_utf8_continuation(static_cast<unsigned char>(b[0]))) break;
    --p;
  }
  return p;
}

std::size_t plaindoc::snap_to_codepoint_right(std::size_t pos) const {
  const std::size_t len = pt_.length();
  if (pos >= len) return len;
  std::size_t p = pos;
  while (p < len) {
    const auto b = pt_.get(p, 1);
    if (b.empty() || !impl::is_utf8_continuation(static_cast<unsigned char>(b[0]))) break;
    ++p;
  }
  return p;
}

void plaindoc::insert_text(std::string_view utf8) {
  if (has_selection()) impl::delete_selection(this);
  if (utf8.empty()) {
    pref_col_valid_ = false;
    impl::notify_caret(this);
    return;
  }
  const std::size_t pos = caret_;
  pt_.insert(pos, utf8);
  const bool mixed = lt_.insert(pt_, pos, utf8.size());
  if (mixed) mixed_eol_ = true;
  caret_ = pos + utf8.size();
  anchor_ = caret_;
  pref_col_valid_ = false;
  impl::notify_changed(this, pos, 0, utf8.size());
}

void plaindoc::backspace() {
  if (has_selection()) {
    impl::delete_selection(this);
    pref_col_valid_ = false;
    return;
  }
  if (caret_ == 0) return;
  std::size_t prev = impl::step_left_codepoint(this, caret_);
  // CRLF: if we're about to delete \n and the previous byte is \r, erase both.
  if (caret_ - prev == 1) {
    const auto b = pt_.get(prev, 1);
    if (!b.empty() && b[0] == '\n' && prev > 0) {
      const auto b2 = pt_.get(prev - 1, 1);
      if (!b2.empty() && b2[0] == '\r') prev -= 1;
    }
  }
  const std::size_t n = caret_ - prev;
  pt_.erase(prev, n);
  lt_.erase(pt_, prev, n);
  caret_ = prev;
  anchor_ = prev;
  pref_col_valid_ = false;
  impl::notify_changed(this, prev, n, 0);
}

void plaindoc::delete_forward() {
  if (has_selection()) {
    impl::delete_selection(this);
    pref_col_valid_ = false;
    return;
  }
  if (caret_ >= pt_.length()) return;
  std::size_t next = impl::step_right_codepoint(this, caret_);
  // CRLF: if we're about to delete \r and the next byte is \n, take both.
  if (next - caret_ == 1) {
    const auto b = pt_.get(caret_, 1);
    if (!b.empty() && b[0] == '\r' && next < pt_.length()) {
      const auto b2 = pt_.get(next, 1);
      if (!b2.empty() && b2[0] == '\n') next += 1;
    }
  }
  const std::size_t n = next - caret_;
  pt_.erase(caret_, n);
  lt_.erase(pt_, caret_, n);
  anchor_ = caret_;
  pref_col_valid_ = false;
  impl::notify_changed(this, caret_, n, 0);
}

void plaindoc::newline() {
  const auto eb = lt_.eol_bytes_for(eol_);
  insert_text(eb);
}

void plaindoc::replace_selection(std::string_view utf8) { insert_text(utf8); }

void plaindoc::set_caret(std::size_t pos, bool extend_selection) {
  pos = std::min(pos, pt_.length());
  pos = snap_to_codepoint_left(pos);
  caret_ = pos;
  if (!extend_selection) anchor_ = pos;
  pref_col_valid_ = false;
  impl::notify_caret(this);
}

void plaindoc::select_all() {
  anchor_ = 0;
  caret_ = pt_.length();
  pref_col_valid_ = false;
  impl::notify_caret(this);
}

std::size_t plaindoc::pos_from_line_col(std::size_t line_idx, std::size_t col_bytes) const noexcept {
  const std::size_t lc = lt_.line_count();
  if (lc == 0) return 0;
  const std::size_t idx = std::min(line_idx, lc - 1);
  const auto l = lt_.at(idx);
  const std::size_t max_col = (l.length > l.eol_bytes) ? (l.length - l.eol_bytes) : 0;
  const std::size_t want = std::min(col_bytes, max_col);
  return l.beg + want;
}

std::string plaindoc::get_line_text(std::size_t line_idx) const {
  const auto l = lt_.at(line_idx);
  const std::size_t n = (l.length > l.eol_bytes) ? (l.length - l.eol_bytes) : 0;
  if (n == 0) return std::string{};
  return pt_.get(l.beg, n);
}

namespace {

bool char_is_word(const piecetable& pt, std::size_t pos) {
  if (pos >= pt.length()) return false;
  const auto b = pt.get(pos, 1);
  if (b.empty()) return false;
  const auto c = static_cast<unsigned char>(b[0]);
  if (c >= 0x80) return true;
  return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

}  // namespace

void plaindoc::move_caret(motion m, bool extend_selection) {
  const std::size_t old_caret = caret_;
  std::size_t new_caret = old_caret;
  bool is_vertical = false;

  switch (m) {
    case motion::left_char: {
      new_caret = impl::step_left_codepoint(this, old_caret);
      // CRLF treated as one motion step (jump over \n + \r).
      if (old_caret >= 1) {
        const auto b = pt_.get(old_caret - 1, 1);
        if (!b.empty() && b[0] == '\n' && old_caret >= 2) {
          const auto b2 = pt_.get(old_caret - 2, 1);
          if (!b2.empty() && b2[0] == '\r') new_caret = old_caret - 2;
        }
      }
      break;
    }
    case motion::right_char: {
      new_caret = impl::step_right_codepoint(this, old_caret);
      if (new_caret - old_caret == 1) {
        const auto b = pt_.get(old_caret, 1);
        if (!b.empty() && b[0] == '\r' && new_caret < pt_.length()) {
          const auto b2 = pt_.get(new_caret, 1);
          if (!b2.empty() && b2[0] == '\n') new_caret += 1;
        }
      }
      break;
    }
    case motion::left_word: {
      std::size_t p = old_caret;
      // Step over any non-word chars first, then over the word.
      while (p > 0) {
        const std::size_t prev = impl::step_left_codepoint(this, p);
        if (char_is_word(pt_, prev)) break;
        p = prev;
      }
      while (p > 0) {
        const std::size_t prev = impl::step_left_codepoint(this, p);
        if (!char_is_word(pt_, prev)) break;
        p = prev;
      }
      new_caret = p;
      break;
    }
    case motion::right_word: {
      std::size_t p = old_caret;
      const std::size_t len = pt_.length();
      while (p < len && char_is_word(pt_, p)) p = impl::step_right_codepoint(this, p);
      while (p < len && !char_is_word(pt_, p)) p = impl::step_right_codepoint(this, p);
      new_caret = p;
      break;
    }
    case motion::line_up: {
      is_vertical = true;
      const auto [li, col] = lt_.line_col(old_caret);
      if (!pref_col_valid_) {
        pref_col_ = col;
        pref_col_valid_ = true;
      }
      if (li == 0) {
        new_caret = 0;
      } else {
        new_caret = pos_from_line_col(li - 1, pref_col_);
      }
      break;
    }
    case motion::line_down: {
      is_vertical = true;
      const auto [li, col] = lt_.line_col(old_caret);
      if (!pref_col_valid_) {
        pref_col_ = col;
        pref_col_valid_ = true;
      }
      if (li + 1 >= lt_.line_count()) {
        new_caret = pt_.length();
      } else {
        new_caret = pos_from_line_col(li + 1, pref_col_);
      }
      break;
    }
    case motion::line_home: {
      const auto [li, _] = lt_.line_col(old_caret);
      new_caret = lt_.at(li).beg;
      break;
    }
    case motion::line_end: {
      const auto [li, _] = lt_.line_col(old_caret);
      const auto l = lt_.at(li);
      new_caret = l.beg + (l.length > l.eol_bytes ? l.length - l.eol_bytes : 0);
      break;
    }
    case motion::doc_home:
      new_caret = 0;
      break;
    case motion::doc_end:
      new_caret = pt_.length();
      break;
  }

  caret_ = new_caret;
  if (!extend_selection) anchor_ = new_caret;
  if (!is_vertical) pref_col_valid_ = false;
  impl::notify_caret(this);
}

}  // namespace swg
