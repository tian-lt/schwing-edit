#pragma once
// std
#include <string>
// swg
#include "resource.hpp"

namespace swg {

class fontengine {
 public:
  explicit fontengine(const std::string& font_path);

 private:
  unique_ft_face ft_face_;
};

}  // namespace swg
