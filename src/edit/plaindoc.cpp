#include "plaindoc.hpp"

namespace swg {

void plaindoc::reset(std::optional<std::string> new_fontpath, std::optional<eol> new_eol) {
  if (new_fontpath.has_value()) {
    fontpath_ = *new_fontpath;
  }
  if (new_eol.has_value()) {
    mixeol_ = ltable_.rebuild(ptable_, *new_eol);
  }
}
void plaindoc::insert(size_t pos, std::string_view data) {
  ptable_.insert(pos, data);
  ltable_.insert(pos, data);
}
void plaindoc::erase(size_t pos, size_t length) { ptable_.erase(pos, length); }

void host::render(rect /*rc*/) {}

}  // namespace swg
