#pragma once
#include <cstddef>
#include <vector>

namespace swg {

class piecetable {
 private:
  struct piece {
    size_t offset = 0;
    size_t length = 0;
    bool is_original = false;
  };

 private:
  std::vector<piece> piecelist_;
};

}  // namespace swg
