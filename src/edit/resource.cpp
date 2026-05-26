// std
#include <format>
#include <stdexcept>
// deps
#include <freetype/freetype.h>
#include FT_LCD_FILTER_H
// swg
#include "resource.hpp"

namespace swg {

FT_Library ft_library = nullptr;
bool ft_lcd_filter_available_ = false;

namespace details {

void ft_face_deleter::operator()(FT_Face ptr) { check_fterror(FT_Done_Face(ptr)); }

void hb_font_deleter::operator()(hb_font_t* ptr) { hb_font_destroy(ptr); }

void hb_buffer_deleter::operator()(hb_buffer_t* ptr) { hb_buffer_destroy(ptr); }

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

void initialize() {
  check_fterror(FT_Init_FreeType(&ft_library));
  // Enable the default LCD filter so FT_LOAD_TARGET_LCD produces filtered
  // subpixel bitmaps. Not all FreeType builds ship with LCD filtering; in
  // that case the call returns FT_Err_Unimplemented_Feature and the atlas
  // gracefully falls back to grayscale at construction time. We must never
  // throw here — this is the only initialization callsite and the editor
  // must come up successfully on every build of FreeType.
  ft_lcd_filter_available_ =
      (FT_Library_SetLcdFilter(ft_library, FT_LCD_FILTER_DEFAULT) == 0);
}
void uninitialize() { check_fterror(FT_Done_FreeType(ft_library)); }

FT_Library get_ft_library() { return ft_library; }
bool ft_lcd_filter_available() { return ft_lcd_filter_available_; }

}  // namespace swg
