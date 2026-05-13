// std
#include <cassert>
#include <stdexcept>

// swg
#include "piecetable.hpp"

namespace swg {

struct piecetable::impl {
  static auto find_piece(piecetable* self, size_t pos, size_t& beg) {
    assert(beg == 0 && "beg must be 0 at the start");
    for (auto iter = self->piecelist_.begin(); iter != self->piecelist_.end(); ++iter) {
      if (beg + iter->length <= pos) {
        beg += iter->length;
        continue;
      }
      return iter;
    }
    return self->piecelist_.end();
  }
};

size_t piecetable::length() const {
  size_t len = 0;
  for (const auto& piece : piecelist_) {
    len += piece.length;
  }
  return len;
}

std::string piecetable::get(size_t pos, size_t length) {
  std::string result;
  if (length == 0) {
    return result;
  }
  size_t beg = 0;
  auto iter = impl::find_piece(this, pos, beg);
  if (iter == piecelist_.end()) {
    throw std::logic_error{"out of range"};
  }
  auto offset = pos - beg;
  while (length > 0) {
    auto len = std::min(iter->length - offset, length);
    result.append(iter->is_original ? initbuf_.substr(iter->offset + offset, len)
                                    : addbuf_.substr(iter->offset + offset, len));
    length -= len;
    ++iter;
    offset = 0;
  }
  return result;
}

void piecetable::insert(size_t pos, std::string_view data) {
  if (data.empty()) {
    return;
  }
  auto add_offset = addbuf_.size();
  addbuf_ += data;
  if (piecelist_.empty()) {
    piecelist_.push_back(piece{.offset = add_offset, .length = data.length()});
    return;
  }

  size_t beg = 0;
  auto iter = impl::find_piece(this, pos, beg);
  if (iter == piecelist_.end()) {
    auto& last = piecelist_.back();
    if (!last.is_original && last.offset + last.length == add_offset) {
      last.length += data.length();
    } else {
      piecelist_.push_back(piece{.offset = add_offset, .length = data.length()});
    }
    return;
  }

  auto offset = pos - beg;
  if (offset == 0) {
    // insertion at a piece boundary; try to extend the previous add-buffer piece
    if (iter != piecelist_.begin()) {
      auto prev = std::prev(iter);
      if (!prev->is_original && prev->offset + prev->length == add_offset) {
        prev->length += data.length();
        return;
      }
    }
    piecelist_.insert(iter,
                      piece{.offset = add_offset, .length = data.length(), .is_original = false});
    return;
  }

  // insertion in the middle of a piece: split it into two and insert the new piece in between
  auto orig = *iter;
  iter->length = offset;
  piece new_piece{.offset = add_offset, .length = data.length(), .is_original = false};
  piece tail_piece{.offset = orig.offset + offset,
                   .length = orig.length - offset,
                   .is_original = orig.is_original};
  piecelist_.insert(std::next(iter), {new_piece, tail_piece});
}

void piecetable::erase(size_t pos, size_t length) {
  if (length == 0) {
    return;
  }
  size_t beg = 0;
  auto iter = impl::find_piece(this, pos, beg);
  if (iter == piecelist_.end()) {
    throw std::logic_error{"out of range"};
  }

  auto offset = pos - beg;
  if (offset > 0) {
    auto avail = iter->length - offset;
    if (length < avail) {
      // erase falls entirely inside this piece: split it in two
      auto orig = *iter;
      iter->length = offset;
      piece tail{.offset = orig.offset + offset + length,
                 .length = orig.length - offset - length,
                 .is_original = orig.is_original};
      piecelist_.insert(std::next(iter), tail);
      return;
    }
    iter->length = offset;
    length -= avail;
    ++iter;
  }

  // remove whole pieces and possibly trim the front of the last partial piece
  while (length > 0) {
    if (iter == piecelist_.end()) {
      throw std::logic_error{"out of range"};
    }
    if (length >= iter->length) {
      length -= iter->length;
      iter = piecelist_.erase(iter);
    } else {
      iter->offset += length;
      iter->length -= length;
      length = 0;
    }
  }
}

}  // namespace swg