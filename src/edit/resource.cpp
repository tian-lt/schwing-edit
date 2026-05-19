// std
#include <format>
#include <stdexcept>
// swg
#include "resource.hpp"

namespace swg {

FT_Library ft_library = nullptr;

namespace details {

void ft_face_deleter::operator()(FT_Face ptr) { check_fterror(FT_Done_Face(ptr)); }

void hb_font_deleter::operator()(hb_font_t* ptr) { hb_font_destroy(ptr); }

}  // namespace details

void check_fterror(FT_Error ec) {
  if (ec) {
    throw std::runtime_error{std::format("freetype error: {}", FT_Error_String(ec))};
  }
}
void check_ptr(void* ptr, const char* message) {
  if (!ptr) {
    throw std::runtime_error{std::format("pointer is null. {}", message)};
  }
}

void initialize() { check_fterror(FT_Init_FreeType(&ft_library)); }
void uninitialize() { check_fterror(FT_Done_FreeType(ft_library)); }

FT_Library get_ft_library() { return ft_library; }

}  // namespace swg
