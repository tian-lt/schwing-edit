#include "plaindoc.hpp"

namespace swg {

void plaindoc::insert(size_t pos, std::string_view data) { ptable_.insert(pos, data); }

}  // namespace swg
