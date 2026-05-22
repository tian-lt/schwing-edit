// std
#include <stdexcept>
// deps
#include <harfbuzz/hb.h>
// swg
#include "textshaper.hpp"

namespace swg {

textshaper::textshaper() : buf_(hb_buffer_create()) {
  check_ptr(buf_.get(), "hb_buffer_create failed.");
}

std::vector<shaped_glyph> textshaper::shape(hb_font_t* font, std::string_view utf8) {
  if (utf8.empty() || font == nullptr) {
    return {};
  }
  hb_buffer_reset(buf_.get());
  hb_buffer_add_utf8(buf_.get(), utf8.data(), static_cast<int>(utf8.size()), 0,
                     static_cast<int>(utf8.size()));
  hb_buffer_set_direction(buf_.get(), HB_DIRECTION_LTR);
  hb_buffer_set_script(buf_.get(), HB_SCRIPT_COMMON);
  hb_buffer_set_language(buf_.get(), hb_language_get_default());
  hb_buffer_guess_segment_properties(buf_.get());

  hb_shape(font, buf_.get(), nullptr, 0);

  unsigned int glyph_count = 0;
  hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buf_.get(), &glyph_count);
  hb_glyph_position_t* positions = hb_buffer_get_glyph_positions(buf_.get(), &glyph_count);

  std::vector<shaped_glyph> out;
  out.reserve(glyph_count);
  for (unsigned int i = 0; i < glyph_count; ++i) {
    out.push_back(shaped_glyph{
        .id = infos[i].codepoint,
        .x_advance = positions[i].x_advance,
        .y_advance = positions[i].y_advance,
        .x_offset = positions[i].x_offset,
        .y_offset = positions[i].y_offset,
        .cluster = infos[i].cluster,
    });
  }
  return out;
}

}  // namespace swg