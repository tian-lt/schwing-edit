#pragma once
// std
#include <cstddef>
#include <string>
#include <string_view>
// schwing
#include "linetable.hpp"
#include "piecetable.hpp"

namespace swg {

class plaindoc {
 public:
  explicit plaindoc(double fontsize, eol eol) : ltable_(eol), fontsize_(fontsize) {}
  void reset(eol eol);
  void insert(size_t pos, std::string_view data);
  void erase(size_t pos, size_t length);
  std::string get(size_t pos, size_t length) const { return ptable_.get(pos, length); }
  size_t length() const { return ptable_.length(); }

 private:
  piecetable ptable_;
  linetable ltable_;
  double fontsize_;
  bool mixeol_ = false;
};

}  // namespace swg
