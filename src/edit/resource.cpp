// std
#include <stdexcept>
// swg
#include "resource.hpp"

namespace swg {

FT_Library ft_library = nullptr;

namespace details {

void ft_face_deleter::operator()(FT_Face ptr) {
  if (FT_Done_Face(ptr)) {
    throw std::runtime_error{"Failed to free FT_Face"};
  }
}

}  // namespace details

void initialize() {
  if (FT_Init_FreeType(&ft_library)) {
    throw std::runtime_error{"Failed to initialize FreeType library"};
  }
}
void uninitialize() {
  if (FT_Done_FreeType(ft_library)) {
    throw std::runtime_error{"Failed to uninitialize FreeType library"};
  }
}

FT_Library get_ft_library() { return ft_library; }

}  // namespace swg