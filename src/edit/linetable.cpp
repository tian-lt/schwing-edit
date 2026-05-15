// swg
#include "linetable.hpp"

namespace swg {

bool linetable::rebuild(const piecetable& ptable, double lineheight, eol eol) {
  eol_ = eol;
  linelist_.clear();
  constexpr size_t BUF_SIZE = 256;
  char buf[BUF_SIZE];
  const size_t total_len = ptable.length();
  size_t pos = 0;
  size_t line_beg = 0;
  bool pending_cr = false;    // last byte seen was a bare '\r' awaiting a possible '\n'
  size_t pending_cr_pos = 0;  // absolute position of that bare '\r'
  unsigned eol_mask = 0;      // bit 0: lf, bit 1: crlf, bit 2: cr
  auto push_line = [&](size_t end_exclusive) {
    linelist_.push_back(
        line{.beg = line_beg, .length = end_exclusive - line_beg, .height = lineheight});
    line_beg = end_exclusive;
  };
  while (pos < total_len) {
    auto chunk = std::min(BUF_SIZE, total_len - pos);
    ptable.get_to(pos, std::span<char>{buf, chunk});
    for (size_t i = 0; i < chunk; ++i) {
      const char c = buf[i];
      const size_t cur = pos + i;
      if (pending_cr) {
        if (c == '\n') {
          eol_mask |= 0b010;  // crlf
          push_line(cur + 1);
          pending_cr = false;
          continue;
        }
        // bare cr finishes the previous line
        eol_mask |= 0b100;  // cr
        push_line(pending_cr_pos + 1);
        pending_cr = false;
        // fall through to process the current byte
      }
      if (c == '\r') {
        pending_cr = true;
        pending_cr_pos = cur;
      } else if (c == '\n') {
        eol_mask |= 0b001;  // lf
        push_line(cur + 1);
      }
    }
    pos += chunk;
  }
  if (pending_cr) {
    eol_mask |= 0b100;  // cr
    push_line(pending_cr_pos + 1);
  }
  if (line_beg < total_len) {
    push_line(total_len);
  }
  // mixed EOLs iff more than one distinct terminator kind was observed
  return (eol_mask & (eol_mask - 1)) != 0;
}

}  // namespace swg
