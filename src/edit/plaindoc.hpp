#pragma once
// std
#include <cstddef>
#include <string>
#include <string_view>
// schwing
#include "piecetable.hpp"

namespace swg {

class plaindoc {
 public:
  void insert(size_t pos, std::string_view data);
  void erase(size_t pos, size_t length);
  std::string get(size_t pos, size_t length) const;
  size_t length() const;

 private:
  piecetable ptable_;
};

}  // namespace swg
