#pragma once
// std
#include <string>
// icu
#include <unicode/uscript.h>
// swg
#include "resource.hpp"

namespace swg {

class fontengine {
  struct impl;

 public:
  explicit fontengine(const std::string& fontpath, double fontsize, int dpi);
  hb_font_t* hbfont() const { return hbfont_.get(); }
  FT_Face ftface() const { return ftface_.get(); }
  double space_advance() const;

 private:
  unique_ft_face ftface_;
  unique_hb_font hbfont_;
};

std::string font_for_script(UScriptCode script);

}  // namespace swg
