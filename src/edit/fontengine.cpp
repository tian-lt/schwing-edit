// std
#include <cassert>
#include <stdexcept>
// deps
#include <freetype/freetype.h>
#include <harfbuzz/hb-ft.h>
#include <harfbuzz/hb.h>
// swg
#include "fontengine.hpp"

namespace swg {

fontengine::fontengine(const std::string& font_path) {
  auto ftlib = get_ft_library();
  assert(ftlib && "freetype library must be initialized.");
  unique_ft_face face;
  if (FT_New_Face(ftlib, font_path.c_str(), 0, std::out_ptr(ft_face_))) {
    throw std::runtime_error{"Failed to load font face from path: " + font_path};
  }
}

}  // namespace swg
