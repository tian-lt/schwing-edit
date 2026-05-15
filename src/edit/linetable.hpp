#pragma once
// std
#include <vector>
// swg
#include "piecetable.hpp"

namespace swg {

enum struct eol { lf, crlf, cr };

class linetable {
 public:
  struct line {
    size_t beg = 0;     // position of the first character of the line
    size_t length = 0;  // length of the line, including linefeed (e.g. \n, \r\n, etc. )
    double height = 0;  // the height of the tallest glyph in the line, in dip
  };

  explicit linetable(eol eol) : eol_(eol) {}
  bool rebuild(const piecetable& ptable, double lineheight, eol eol);
  const std::vector<line>& lines() const noexcept { return linelist_; }
  eol get_eol() const noexcept { return eol_; }

 private:
  std::vector<line> linelist_;
  eol eol_;
};

}  // namespace swg
