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

struct fontengine::impl {
  static void reset(fontengine* self, const std::string& fontpath, double fontsize, int dpi) {
    auto ftlib = get_ft_library();
    assert(ftlib && "freetype library must be initialized.");
    unique_ft_face face;
    check_fterror(FT_New_Face(ftlib, fontpath.c_str(), 0, std::out_ptr(face)));
    check_fterror(FT_Set_Char_Size(face.get(), 0, (FT_F26Dot6)fontsize * 64, dpi, dpi));
    check_ptr(hb_ft_face_create_referenced(face.get()), "hb_ft_face_create_referenced failed.");
    self->ftface_ = std::move(face);
    self->hbfont_ = unique_hb_font{hb_ft_font_create(self->ftface_.get(), nullptr)};
  }
};

fontengine::fontengine(const std::string& fontpath, double fontsize, int dpi) {
  impl::reset(this, fontpath, fontsize, dpi);
}

}  // namespace swg
