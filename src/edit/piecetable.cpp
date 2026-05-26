// std
#include <cassert>
#include <cstring>
#include <stdexcept>

// swg
#include "piecetable.hpp"

namespace swg {

struct piecetable::impl {
  static auto find_piece(const piecetable* self, size_t pos) {
    struct {
      size_t idx = 0;
      size_t beg = 0;
    } result;
    for (; result.idx < self->piecelist_.size(); ++result.idx) {
      if (result.beg + self->piecelist_[result.idx].length <= pos) {
        result.beg += self->piecelist_[result.idx].length;
        continue;
      }
      return result;
    }
    return result;
  }

  static void copy_out(const piecetable* self, size_t pos, size_t length, char* out) {
    if (length == 0) {
      if (pos > self->length()) {
        throw std::out_of_range{"pos out of range"};
      }
      return;
    }
    auto [idx, beg] = find_piece(self, pos);
    if (idx == self->piecelist_.size()) {
      throw std::out_of_range{"pos out of range"};
    }
    auto offset = pos - beg;
    while (length > 0) {
      if (idx == self->piecelist_.size()) {
        throw std::out_of_range{"length out of range"};
      }
      const auto& piece = self->piecelist_[idx];
      auto len = std::min(piece.length - offset, length);
      const auto& buf = piece.is_original ? self->initbuf_ : std::string_view{self->addbuf_};
      std::memcpy(out, buf.data() + piece.offset + offset, len);
      out += len;
      length -= len;
      ++idx;
      offset = 0;
    }
  }
};

std::string piecetable::get(size_t pos, size_t length) const {
  std::string result;
  result.resize(length);
  impl::copy_out(this, pos, length, result.data());
  return result;
}

void piecetable::get_to(size_t pos, std::span<char> out) const {
  impl::copy_out(this, pos, out.size(), out.data());
}

void piecetable::insert(size_t pos, std::string_view data) {
  if (data.empty()) {
    return;
  }
  auto add_offset = addbuf_.size();
  addbuf_ += data;
  length_ += data.length();
  if (piecelist_.empty()) {
    piecelist_.push_back(piece{.offset = add_offset, .length = data.length()});
    return;
  }

  auto [idx, beg] = impl::find_piece(this, pos);
  if (idx == piecelist_.size()) {
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
    if (idx != 0) {
      auto& prev = piecelist_[idx - 1];
      if (!prev.is_original && prev.offset + prev.length == add_offset) {
        prev.length += data.length();
        return;
      }
    }
    piecelist_.insert(piecelist_.begin() + idx,
                      piece{.offset = add_offset, .length = data.length(), .is_original = false});
    return;
  }

  // insertion in the middle of a piece: split it into two and insert the new piece in between
  auto orig = piecelist_[idx];
  piecelist_[idx].length = offset;
  piece new_piece{.offset = add_offset, .length = data.length(), .is_original = false};
  piece tail_piece{.offset = orig.offset + offset,
                   .length = orig.length - offset,
                   .is_original = orig.is_original};
  piecelist_.insert(piecelist_.begin() + idx + 1, {new_piece, tail_piece});
}

void piecetable::erase(size_t pos, size_t length) {
  if (length == 0) {
    return;
  }
  auto [idx, beg] = impl::find_piece(this, pos);
  if (idx == piecelist_.size()) {
    throw std::out_of_range{"pos out of range"};
  }
  if (pos + length > length_) {
    throw std::out_of_range{"length out of range"};
  }
  length_ -= length;

  auto offset = pos - beg;
  if (offset > 0) {
    auto& cur = piecelist_[idx];
    auto avail = cur.length - offset;
    if (length < avail) {
      // erase falls entirely inside this piece: split it in two
      auto orig = cur;
      cur.length = offset;
      piece tail{.offset = orig.offset + offset + length,
                 .length = orig.length - offset - length,
                 .is_original = orig.is_original};
      piecelist_.insert(piecelist_.begin() + idx + 1, tail);
      return;
    }
    cur.length = offset;
    length -= avail;
    ++idx;
  }

  // remove whole pieces and possibly trim the front of the last partial piece
  while (length > 0) {
    if (idx == piecelist_.size()) {
      throw std::out_of_range{"pos out of range"};
    }
    auto& cur = piecelist_[idx];
    if (length >= cur.length) {
      length -= cur.length;
      piecelist_.erase(piecelist_.begin() + idx);
    } else {
      cur.offset += length;
      cur.length -= length;
      length = 0;
    }
  }
}

bool piecetable::references_initbuf() const noexcept {
  for (const auto& p : piecelist_) {
    if (p.is_original && p.length > 0) return true;
  }
  return false;
}

void piecetable::detach_initbuf() {
  if (!references_initbuf()) {
    // Nothing depends on the external buffer; just drop the view.
    initbuf_ = {};
    return;
  }
  // Copy every original-referencing piece into the addbuf and rewrite it as
  // an add-buffer piece. Use a single append per piece so we preserve byte
  // ordering and don't disturb adjacent add-buffer pieces' offsets.
  for (auto& p : piecelist_) {
    if (!p.is_original || p.length == 0) continue;
    size_t new_offset = addbuf_.size();
    addbuf_.append(initbuf_.data() + p.offset, p.length);
    p.offset = new_offset;
    p.is_original = false;
  }
  initbuf_ = {};
}

}  // namespace swg