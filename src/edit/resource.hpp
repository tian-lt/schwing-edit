#pragma once
// std
#include <memory>
#include <type_traits>
// deps
#include <freetype/freetype.h>
#include <harfbuzz/hb-ft.h>
#include <harfbuzz/hb.h>

namespace swg {

namespace details {
struct ft_face_deleter {
  void operator()(FT_Face);
};
struct hb_font_deleter {
  void operator()(hb_font_t*);
};
}  // namespace details

using unique_ft_face = std::unique_ptr<std::remove_pointer_t<FT_Face>, details::ft_face_deleter>;
using unique_hb_font = std::unique_ptr<hb_font_t, details::hb_font_deleter>;

FT_Library get_ft_library();
void initialize();
void uninitialize();

void check_fterror(FT_Error error_code);
void check_ptr(void* ptr, const char* message);
inline void check_ptr(void* ptr) { check_ptr(ptr, ""); }

}  // namespace swg
