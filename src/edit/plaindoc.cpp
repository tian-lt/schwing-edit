#include "plaindoc.hpp"

namespace swg {

void plaindoc::reset(eol eol) { mixeol_ = ltable_.rebuild(ptable_, eol); }
void plaindoc::insert(size_t pos, std::string_view data) { ptable_.insert(pos, data); }
void plaindoc::erase(size_t pos, size_t length) { ptable_.erase(pos, length); }

}  // namespace swg
