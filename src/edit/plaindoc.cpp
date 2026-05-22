// std
#include <algorithm>
#include <cassert>
// gl
#include <glad/glad.h>
// swg
#include "plaindoc.hpp"

namespace swg {

void plaindoc::reset(std::optional<std::string> new_fontpath, std::optional<eol> new_eol) {
  if (new_fontpath.has_value()) {
    fontpath_ = *new_fontpath;
  }
  if (new_eol.has_value()) {
    eol_ = *new_eol;
    mixeol_ = ltable_.rebuild(ptable_, *new_eol);
  }
}
void plaindoc::insert(size_t pos, std::string_view data) {
  ptable_.insert(pos, data);
  mixeol_ = ltable_.rebuild(ptable_, eol_);
}
void plaindoc::erase(size_t pos, size_t length) {
  ptable_.erase(pos, length);
  mixeol_ = ltable_.rebuild(ptable_, eol_);
}

struct host::impl {
  static bool is_utf8_continuation(char c) { return (static_cast<unsigned char>(c) & 0xC0) == 0x80; }

  static size_t previous_character_start(const plaindoc* doc, size_t pos) {
    if (pos == 0) {
      return 0;
    }
    if (pos >= 2 && doc->get(pos - 2, 2) == "\r\n") {
      return pos - 2;
    }
    size_t beg = pos - 1;
    const size_t min_beg = pos > 4 ? pos - 4 : 0;
    while (beg > min_beg && is_utf8_continuation(doc->get(beg, 1)[0])) {
      --beg;
    }
    return beg;
  }

  static size_t next_character_end(const plaindoc* doc, size_t pos) {
    const size_t length = doc->length();
    if (pos >= length) {
      return length;
    }
    if (pos + 1 < length && doc->get(pos, 2) == "\r\n") {
      return pos + 2;
    }
    size_t end = pos + 1;
    while (end < length && is_utf8_continuation(doc->get(end, 1)[0])) {
      ++end;
    }
    return end;
  }

  static void post_edit(host* self, size_t pos_before, size_t pos_after) {
    auto l0 = self->doc->ltable_.line_at_pos(pos_before);
    auto l1 = self->doc->ltable_.line_at_pos(pos_after);
    l0 = l0 > l1 ? l1 : l0;
  }
};
void host::render(rect /*rc*/) {}
void host::insert_char(std::string_view u8char) {
  assert(u8char != "\r" && u8char != "\n" && u8char != "\b");
  size_t before = inspos_;
  doc->insert(inspos_, u8char);
  inspos_ += u8char.length();
  impl::post_edit(this, before, inspos_);
}
void host::caret_pos(size_t pos) { inspos_ = std::min(pos, doc->length()); }
void host::move_left() { inspos_ = impl::previous_character_start(doc, inspos_); }
void host::move_right() { inspos_ = impl::next_character_end(doc, inspos_); }
void host::erase_char() {
  if (doc->length() == 0 || inspos_ == 0) {
    return;
  }
  size_t before = inspos_;
  size_t erased_beg = impl::previous_character_start(doc, inspos_);
  doc->erase(erased_beg, inspos_ - erased_beg);
  inspos_ = erased_beg;
  impl::post_edit(this, before, inspos_);
}
void host::delete_char() {
  if (doc->length() == 0 || inspos_ >= doc->length()) {
    return;
  }
  size_t before = inspos_;
  size_t erased_end = impl::next_character_end(doc, inspos_);
  doc->erase(inspos_, erased_end - inspos_);
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
