#pragma once
// std
#include <cstdint>
#include <string_view>
#include <vector>
// swg
#include "resource.hpp"

namespace swg {

struct shaped_glyph {
  uint32_t id = 0;          // glyph index in the font (not a codepoint)
  int32_t x_advance = 0;    // 26.6 px
  int32_t y_advance = 0;    // 26.6 px
  int32_t x_offset = 0;     // 26.6 px
  int32_t y_offset = 0;     // 26.6 px
  uint32_t cluster = 0;     // byte offset into the source utf8 string
};

class textshaper {
 public:
  textshaper();

  // Shape `utf8` with `font`. Returns one shaped_glyph per output glyph.
  // The shaper assumes a left-to-right Latin/common script by default; callers
  // that need other scripts should extend this API.
  std::vector<shaped_glyph> shape(hb_font_t* font, std::string_view utf8);

 private:
  unique_hb_buffer buf_;
};

}  // namespace swg