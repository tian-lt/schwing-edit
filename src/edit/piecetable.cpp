// std
#include <stdexcept>

// swg
#include "piecetable.hpp"

namespace swg {

struct piecetable::impl {
  static auto find_piece(piecetable* self, size_t pos) {
    size_t idx = 0;
    for (auto iter = self->piecelist_.begin(); iter != self->piecelist_.end(); ++iter) {
      if (idx + iter->length <= pos) {
        idx += iter->length;
        continue;
      }
      return iter;
    }
    return self->piecelist_.end();
  }
};

void piecetable::insert(size_t pos, std::string_view data) {
  auto add_offset = addbuf_.size();
  addbuf_ += data;
  if (piecelist_.empty()) {
    piecelist_.push_back(piece{.offset = add_offset, .length = data.length()});
    return;
  }

  auto iter = impl::find_piece(this, pos);
  if (iter == piecelist_.end()) {
    auto& last = piecelist_.back();
    if (!last.is_original && last.offset + last.length == add_offset) {
      last.length += data.length();
    } else {
      piecelist_.push_back(piece{.offset = add_offset, .length = data.length()});
    }
  } else {
  }
}

}  // namespace swg