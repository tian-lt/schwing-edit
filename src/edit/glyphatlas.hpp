#pragma once

// std
#include <vector>
// swg
#include "resource.hpp"

namespace swg {

class glyphatlas {
  struct impl;

 public:
  explicit glyphatlas(unsigned width, unsigned height);

 private:
  struct shelf {};
  struct page {};
  unique_gl_texture texarr_;
  std::vector<page> pages_;
  unsigned width_ = 0;
  unsigned height_ = 0;
};

}  // namespace swg
