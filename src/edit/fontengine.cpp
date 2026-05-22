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
  static void reset(fontengine* self, const std::string& fontpath, double pixel_size) {
    auto ftlib = get_ft_library();
    assert(ftlib && "freetype library must be initialized.");
    unique_ft_face face;
    check_fterror(FT_New_Face(ftlib, fontpath.c_str(), 0, std::out_ptr(face)));
    check_fterror(
        FT_Set_Pixel_Sizes(face.get(), 0, static_cast<FT_UInt>(pixel_size + 0.5)));
    unique_hb_font hbfont{hb_ft_font_create_referenced(face.get())};
    check_ptr(hbfont.get(), "hb_ft_font_create_referenced failed.");
    self->ftface_ = std::move(face);
    self->hbfont_ = std::move(hbfont);
    self->pixel_size_ = pixel_size;
  }
};

fontengine::fontengine(const std::string& fontpath, double pixel_size) {
  impl::reset(this, fontpath, pixel_size);
}

int fontengine::line_height_px() const {
  return static_cast<int>(ftface_->size->metrics.height >> 6);
}
int fontengine::ascent_px() const {
  return static_cast<int>(ftface_->size->metrics.ascender >> 6);
}
int fontengine::descent_px() const {
  return static_cast<int>(-(ftface_->size->metrics.descender >> 6));
}
int fontengine::max_advance_px() const {
  return static_cast<int>(ftface_->size->metrics.max_advance >> 6);
}

}  // namespace swg
