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
struct hb_buffer_deleter {
  void operator()(hb_buffer_t*);
};
}  // namespace details

using unique_ft_face = std::unique_ptr<std::remove_pointer_t<FT_Face>, details::ft_face_deleter>;
using unique_hb_font = std::unique_ptr<hb_font_t, details::hb_font_deleter>;
using unique_hb_buffer = std::unique_ptr<hb_buffer_t, details::hb_buffer_deleter>;

FT_Library get_ft_library();
// True iff the FreeType library was built with LCD filtering support. Set by
// `initialize()` based on the result of `FT_Library_SetLcdFilter`. When false,
// the glyph atlas must use grayscale rasterization (FT_LOAD_DEFAULT) — the
// LCD code path would still work but `FT_LOAD_TARGET_LCD` would produce
// unfiltered bitmaps that look colored-fringey on every screen.
bool ft_lcd_filter_available();
void initialize();
void uninitialize();

void check_fterror(FT_Error error_code);
void check_ptr(void* ptr, const char* message);
inline void check_ptr(void* ptr) { check_ptr(ptr, ""); }

}  // namespace swg
