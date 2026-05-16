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
}  // namespace details

using unique_ft_face = std::unique_ptr<std::remove_pointer_t<FT_Face>, details::ft_face_deleter>;

FT_Library get_ft_library();
void initialize();
void uninitialize();

}  // namespace swg
