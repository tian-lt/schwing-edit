#pragma once
// std
#include <string>
// swg
#include "resource.hpp"

namespace swg {

class fontengine {
  struct impl;

 public:
  explicit fontengine(const std::string& fontpath, double fontsize);
  hb_font_t* hbfont() const { return hbfont_.get(); }
  FT_Face ftface() const { return ftface_.get(); }

 private:
  unique_ft_face ftface_;
  unique_hb_font hbfont_;
};

}  // namespace swg
