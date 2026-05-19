// std
#include <algorithm>
#include <cassert>
// swg
#include "plaindoc.hpp"

namespace swg {

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
void plaindoc::erase(size_t pos, size_t length) { ptable_.erase(pos, length); }

void host::render(rect /*rc*/) {}

void host::insert_char(std::string_view u8char) {
  assert(u8char != "\r" && u8char != "\n" && u8char != "\b");
  doc->insert(inspos_, u8char);
  inspos_ += u8char.length();
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
  inspos_ = e;
}
void host::linefeed() {
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
}

}  // namespace swg
