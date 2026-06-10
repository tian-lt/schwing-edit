#pragma once
// std
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#ifdef SWGUT
#include <gtest/gtest_prod.h>
namespace swg::ut::piecetable_ut {
struct insert;
struct erase;
struct get;
struct coalesce;
}  // namespace swg::ut::piecetable_ut
#endif

namespace swg {

class piecetable {
 public:
  piecetable() = default;
  explicit piecetable(std::string_view initbuf);

  void reset(std::string_view initbuf);

  // Insert utf8 bytes at byte position pos. Throws std::out_of_range if pos > length().
  void insert(std::size_t pos, std::string_view bytes);

  // Erase n utf8 bytes at byte position pos. Throws std::out_of_range if pos+n > length().
  void erase(std::size_t pos, std::size_t n);

  // Return n bytes starting at byte position pos as an owned string.
  // Throws std::out_of_range if pos+n > length().
  std::string get(std::size_t pos, std::size_t n) const;

  // Copy n bytes starting at byte position pos into `out` (size >= n).
  // Throws std::out_of_range if pos+n > length().
  void get_to(std::size_t pos, std::size_t n, std::span<char> out) const;

  // Total length in bytes. O(1).
  std::size_t length() const noexcept { return length_; }

  // Copy initbuf bytes into addbuf so external initbuf storage may be freed.
  // After materialize(), no piece references the original initbuf.
  void materialize();

  // Concatenate the entire buffer into one owned string. O(n).
  std::string str() const;

 private:
  struct piece {
    std::size_t offset;  // offset within source buffer
    std::size_t length;  // bytes
    bool original;       // true: initbuf_, false: addbuf_
  };

  struct impl;

  std::string_view initbuf_;
  std::string addbuf_;
  std::vector<piece> piecelist_;
  std::size_t length_ = 0;
  std::size_t last_insert_pos_ = static_cast<std::size_t>(-1);

#ifdef SWGUT
  friend struct swg::ut::piecetable_ut::insert;
  friend struct swg::ut::piecetable_ut::erase;
  friend struct swg::ut::piecetable_ut::get;
  friend struct swg::ut::piecetable_ut::coalesce;
#endif
};

}  // namespace swg
