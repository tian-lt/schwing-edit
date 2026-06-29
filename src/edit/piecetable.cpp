// std
#include <cassert>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <system_error>

// platform
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#error "piecetable::from_file is currently Win32 only"
#endif

// deps
#include <mio/mmap.hpp>

// swg
#include "piecetable.hpp"

namespace swg {

namespace {
struct mapped_file {
#ifdef _WIN32
  HANDLE handle = INVALID_HANDLE_VALUE;
#else
#error "piecetable::from_file is currently Win32 only"
#endif
  mio::mmap_source mmap;
  ~mapped_file() {
    mmap.unmap();
#ifdef _WIN32
    if (handle != INVALID_HANDLE_VALUE) {
      ::CloseHandle(handle);
    }
#else
#error "piecetable::from_file is currently Win32 only"
#endif
  }
};
}  // namespace

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

piecetable piecetable::from_file(const std::filesystem::path& path) {
  constexpr size_t occupy_size = 10 * 1024u * 1024u;  // 10MB
  piecetable table;
  auto fsize = std::filesystem::file_size(path);
  if (0 < fsize && fsize < occupy_size) {
    std::ifstream fs;
    fs.exceptions(std::ios::failbit | std::ios::badbit);
    fs.open(path, std::ios::binary);
    table.data_.resize(fsize);
    fs.read(table.data_.data(), fsize);
    table.initbuf_ = std::string_view{table.data_.data(), fsize};
    table.piecelist_ = {piece{.offset = 0, .length = fsize, .is_original = true}};
  } else if (fsize >= occupy_size) {
    auto owner = std::make_shared<mapped_file>();
#ifdef _WIN32
    // FILE_SHARE_READ only: other processes may read but cannot write or delete the file.
    owner->handle = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (owner->handle == INVALID_HANDLE_VALUE) {
      throw std::system_error{static_cast<int>(::GetLastError()), std::system_category(),
                              "failed to open file"};
    }
#else
#error "piecetable::from_file is currently Win32 only"
#endif
    std::error_code ec;
#ifdef _WIN32
    owner->mmap.map(owner->handle, 0, mio::map_entire_file, ec);
#else
#error "piecetable::from_file is currently Win32 only"
#endif
    if (ec) {
      throw std::system_error{ec, "failed to mmap file"};
    }
    table.initbuf_ = std::string_view{owner->mmap.data(), owner->mmap.size()};
    table.piecelist_ = {piece{.offset = 0, .length = owner->mmap.size(), .is_original = true}};
    table.mmap_ = std::move(owner);
  }
  return table;
}

size_t piecetable::length() const {
  size_t len = 0;
  for (const auto& p : piecelist_) {
    len += p.length;
  }
  return len;
}

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

}  // namespace swg