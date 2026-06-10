// std
#include <algorithm>
#include <array>
#include <stdexcept>
#include <utility>
// swg
#include "piecetable.hpp"

namespace swg {

struct piecetable::impl {
  // Locate the piece containing absolute byte position `pos`.
  // Returns (piece_index, offset_within_piece). pos may equal length() in which
  // case piece_index == piecelist_.size() and offset_within_piece == 0.
  static std::pair<std::size_t, std::size_t> locate(const piecetable* self, std::size_t pos) {
    std::size_t walk = 0;
    for (std::size_t i = 0; i < self->piecelist_.size(); ++i) {
      const auto& p = self->piecelist_[i];
      if (pos < walk + p.length) {
        return {i, pos - walk};
      }
      walk += p.length;
    }
    return {self->piecelist_.size(), 0};
  }

  static std::string_view src(const piecetable* self, const piece& p) {
    return p.original ? self->initbuf_ : std::string_view{self->addbuf_};
  }

  static void copy_out(const piecetable* self, std::size_t pos, std::size_t n, char* out) {
    if (n == 0) return;
    auto [pi, off] = locate(self, pos);
    std::size_t remaining = n;
    while (remaining > 0 && pi < self->piecelist_.size()) {
      const auto& p = self->piecelist_[pi];
      auto sv = src(self, p);
      const std::size_t avail = p.length - off;
      const std::size_t take = std::min(avail, remaining);
      std::copy_n(sv.data() + p.offset + off, take, out);
      out += take;
      remaining -= take;
      ++pi;
      off = 0;
    }
    if (remaining > 0) {
      throw std::out_of_range{"piecetable::copy_out range exceeds length"};
    }
  }
};

piecetable::piecetable(std::string_view initbuf) { reset(initbuf); }

void piecetable::reset(std::string_view initbuf) {
  initbuf_ = initbuf;
  addbuf_.clear();
  piecelist_.clear();
  length_ = initbuf.size();
  last_insert_pos_ = static_cast<std::size_t>(-1);
  if (!initbuf.empty()) {
    piecelist_.push_back({0, initbuf.size(), true});
  }
}

void piecetable::insert(std::size_t pos, std::string_view bytes) {
  if (pos > length_) {
    throw std::out_of_range{"piecetable::insert pos out of range"};
  }
  if (bytes.empty()) return;

  const std::size_t add_off = addbuf_.size();
  addbuf_.append(bytes);

  auto [pi, off] = impl::locate(this, pos);

  // Coalesce: inserting right after the previous insert's add-buf piece.
  if (pos == last_insert_pos_ && off == 0 && pi > 0) {
    auto& prev = piecelist_[pi - 1];
    if (!prev.original && prev.offset + prev.length == add_off) {
      prev.length += bytes.size();
      length_ += bytes.size();
      last_insert_pos_ = pos + bytes.size();
      return;
    }
  }

  const piece newp{.offset = add_off, .length = bytes.size(), .original = false};

  if (pi == piecelist_.size()) {
    piecelist_.push_back(newp);
  } else if (off == 0) {
    piecelist_.insert(piecelist_.begin() + static_cast<std::ptrdiff_t>(pi), newp);
  } else {
    // split piecelist_[pi] into two; insert newp between
    piece& orig = piecelist_[pi];
    const piece tail{.offset = orig.offset + off,
                     .length = orig.length - off,
                     .original = orig.original};
    orig.length = off;
    const std::array<piece, 2> two{newp, tail};
    piecelist_.insert(piecelist_.begin() + static_cast<std::ptrdiff_t>(pi) + 1,
                      two.begin(), two.end());
  }

  length_ += bytes.size();
  last_insert_pos_ = pos + bytes.size();
}

void piecetable::erase(std::size_t pos, std::size_t n) {
  if (n == 0) return;
  if (pos + n > length_) {
    throw std::out_of_range{"piecetable::erase range out of range"};
  }
  last_insert_pos_ = static_cast<std::size_t>(-1);

  auto [pi, off] = impl::locate(this, pos);
  std::size_t remaining = n;

  // Trim head of pi if off > 0.
  if (off > 0 && pi < piecelist_.size()) {
    auto& p = piecelist_[pi];
    const std::size_t head_keep = off;
    const std::size_t head_after = p.length - off;
    if (head_after >= remaining) {
      // erase a slice inside this piece; possibly split.
      if (head_after == remaining) {
        p.length = head_keep;
      } else {
        const std::size_t tail_off = p.offset + off + remaining;
        const std::size_t tail_len = head_after - remaining;
        const bool orig = p.original;
        p.length = head_keep;
        piecelist_.insert(piecelist_.begin() + static_cast<std::ptrdiff_t>(pi) + 1,
                          piece{tail_off, tail_len, orig});
      }
      length_ -= n;
      // drop zero-length piece if any
      if (piecelist_[pi].length == 0) {
        piecelist_.erase(piecelist_.begin() + static_cast<std::ptrdiff_t>(pi));
      }
      return;
    }
    // erase tail of this piece, advance.
    p.length = head_keep;
    remaining -= head_after;
    ++pi;
  }

  // Drop whole pieces while they fit.
  std::size_t drop_first = pi;
  std::size_t drop_count = 0;
  while (remaining > 0 && pi < piecelist_.size() && piecelist_[pi].length <= remaining) {
    remaining -= piecelist_[pi].length;
    ++drop_count;
    ++pi;
  }
  if (drop_count > 0) {
    piecelist_.erase(piecelist_.begin() + static_cast<std::ptrdiff_t>(drop_first),
                     piecelist_.begin() + static_cast<std::ptrdiff_t>(drop_first + drop_count));
    pi = drop_first;
  }

  // Trim head of remaining current piece.
  if (remaining > 0 && pi < piecelist_.size()) {
    piecelist_[pi].offset += remaining;
    piecelist_[pi].length -= remaining;
  }

  length_ -= n;
}

std::string piecetable::get(std::size_t pos, std::size_t n) const {
  if (pos + n > length_) {
    throw std::out_of_range{"piecetable::get range out of range"};
  }
  std::string out;
  out.resize(n);
  if (n > 0) impl::copy_out(this, pos, n, out.data());
  return out;
}

void piecetable::get_to(std::size_t pos, std::size_t n, std::span<char> out) const {
  if (out.size() < n) {
    throw std::out_of_range{"piecetable::get_to output span too small"};
  }
  if (pos + n > length_) {
    throw std::out_of_range{"piecetable::get_to range out of range"};
  }
  if (n > 0) impl::copy_out(this, pos, n, out.data());
}

void piecetable::materialize() {
  if (initbuf_.empty()) return;
  // Copy any initbuf-referenced bytes into addbuf and rewrite pieces.
  for (auto& p : piecelist_) {
    if (p.original) {
      const std::size_t new_off = addbuf_.size();
      addbuf_.append(initbuf_.data() + p.offset, p.length);
      p.offset = new_off;
      p.original = false;
    }
  }
  initbuf_ = {};
  last_insert_pos_ = static_cast<std::size_t>(-1);
}

std::string piecetable::str() const {
  std::string out;
  out.resize(length_);
  if (length_ > 0) impl::copy_out(this, 0, length_, out.data());
  return out;
}

}  // namespace swg
