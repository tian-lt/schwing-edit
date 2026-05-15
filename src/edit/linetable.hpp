#pragma once
// std
#include <vector>
// swg
#include "piecetable.hpp"

namespace swg {

enum struct eol { lf, crlf, cr };

#ifdef SWGUT
namespace ut::linetable_ut {
struct linetable_tests_run_Test;
struct linetable_tests_rebuild_clears_previous_lines_Test;
struct linetable_tests_rebuild_uses_piecetable_after_edits_Test;
};  // namespace ut::linetable_ut
#endif  // SWGUT

class linetable {
#ifdef SWGUT
  FRIEND_TEST(ut::linetable_ut::linetable_tests, run);
  FRIEND_TEST(ut::linetable_ut::linetable_tests, rebuild_clears_previous_lines);
  FRIEND_TEST(ut::linetable_ut::linetable_tests, rebuild_uses_piecetable_after_edits);
#endif  // SWGUT

  struct line {
    size_t beg = 0;     // position of the first character of the line
    size_t length = 0;  // length of the line, including linefeed (e.g. \n, \r\n, etc. )
    double height = 0;  // the height of the tallest glyph in the line, in dip
  };

 public:
  explicit linetable(eol eol) : eol_(eol) {}
  bool rebuild(const piecetable& ptable, double lineheight, eol eol);

 private:
  std::vector<line> linelist_;
  eol eol_;
};

}  // namespace swg
