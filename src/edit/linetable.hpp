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
struct linetable_insert_tests_run_Test;
struct linetable_insert_matches_rebuild_param_equivalent_Test;
struct linetable_tests_insert_matches_rebuild_Test;
struct linetable_tests_insert_into_empty_table_Test;
};  // namespace ut::linetable_ut
#endif  // SWGUT

class linetable {
#ifdef SWGUT
  FRIEND_TEST(ut::linetable_ut::linetable_tests, run);
  FRIEND_TEST(ut::linetable_ut::linetable_tests, rebuild_clears_previous_lines);
  FRIEND_TEST(ut::linetable_ut::linetable_tests, rebuild_uses_piecetable_after_edits);
  FRIEND_TEST(ut::linetable_ut::linetable_insert_tests, run);
  FRIEND_TEST(ut::linetable_ut::linetable_insert_matches_rebuild_param, equivalent);
  FRIEND_TEST(ut::linetable_ut::linetable_tests, insert_matches_rebuild);
  FRIEND_TEST(ut::linetable_ut::linetable_tests, insert_into_empty_table);
#endif  // SWGUT

  struct line {
    size_t beg = 0;     // position of the first character of the line
    size_t length = 0;  // length of the line, including linefeed (e.g. \n, \r\n, etc. )
  };

 public:
  explicit linetable(eol eol) : eol_(eol) {}
  bool rebuild(const piecetable& ptable, eol eol);
  bool insert(size_t pos, std::string_view data);
  size_t line_at_pos(size_t pos) const;

 private:
  std::vector<line> linelist_;
  eol eol_;
};

}  // namespace swg
