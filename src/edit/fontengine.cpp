// std
#include <cassert>
#include <filesystem>
#include <stdexcept>
// deps
#include <freetype/freetype.h>
#include <harfbuzz/hb-ft.h>
#include <harfbuzz/hb.h>
// swg
#include "fontengine.hpp"

namespace swg {

namespace {

const char* default_font_file(UScriptCode script) {
  switch (script) {
    case USCRIPT_LATIN:
    case USCRIPT_GREEK:
    case USCRIPT_CYRILLIC:
    case USCRIPT_COMMON:
    case USCRIPT_INHERITED:
      return "consola.ttf";
    case USCRIPT_HAN:
    case USCRIPT_BOPOMOFO:
      return "msyh.ttc";
    case USCRIPT_HIRAGANA:
    case USCRIPT_KATAKANA:
      return "msgothic.ttc";
    case USCRIPT_HANGUL:
      return "malgun.ttf";
    case USCRIPT_ARABIC:
    case USCRIPT_HEBREW:
      return "tahoma.ttf";
    case USCRIPT_DEVANAGARI:
    case USCRIPT_BENGALI:
    case USCRIPT_GURMUKHI:
    case USCRIPT_GUJARATI:
    case USCRIPT_ORIYA:
    case USCRIPT_TAMIL:
    case USCRIPT_TELUGU:
    case USCRIPT_KANNADA:
    case USCRIPT_MALAYALAM:
    case USCRIPT_SINHALA:
      return "Nirmala.ttc";
    case USCRIPT_THAI:
    case USCRIPT_LAO:
      return "LeelawUI.ttf";
    case USCRIPT_ARMENIAN:
    case USCRIPT_GEORGIAN:
      return "sylfaen.ttf";
    case USCRIPT_ETHIOPIC:
    case USCRIPT_TIFINAGH:
    case USCRIPT_VAI:
    case USCRIPT_NKO:
      return "ebrima.ttf";
    case USCRIPT_CANADIAN_ABORIGINAL:
    case USCRIPT_CHEROKEE:
      return "gadugi.ttf";
    case USCRIPT_SYMBOLS_EMOJI:
      return "seguiemj.ttf";
    default:
      return "arial.ttf";
  }
}

}  // namespace

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

double fontengine::space_advance() const {
  hb_codepoint_t glyph = 0;
  if (!hb_font_get_nominal_glyph(hbfont_.get(), ' ', &glyph)) {
    return 0.0;
  }
  return hb_font_get_glyph_h_advance(hbfont_.get(), glyph) / 64.0;
}

std::string font_for_script(UScriptCode script) {
  const char* file = default_font_file(script);
#ifdef _WIN32
  const std::filesystem::path fonts_dir{R"(C:\Windows\Fonts)"};
#else
#error "font_for_script: system font directory is only implemented for Windows."
#endif
  std::filesystem::path path = fonts_dir / file;
  std::error_code ec;
  if (std::filesystem::exists(path, ec)) {
    return path.string();
  } else {
    throw std::runtime_error("font_for_script: font file not found: " + path.string());
  }
}

}  // namespace swg
