#pragma once
// std
#include <string>
// swg
#include "resource.hpp"

namespace swg {

class fontengine {
  struct impl;

 public:
  explicit fontengine(const std::string& fontpath, double pixel_size);

  FT_Face face() const { return ftface_.get(); }
  hb_font_t* hbfont() const { return hbfont_.get(); }
  double pixel_size() const { return pixel_size_; }

  // Metrics in integer pixels (face must have been sized via FT_Set_Pixel_Sizes).
  int line_height_px() const;
  int ascent_px() const;
  int descent_px() const;
  int max_advance_px() const;

 private:
  unique_ft_face ftface_;
  unique_hb_font hbfont_;
  double pixel_size_ = 0;
};

}  // namespace swg
