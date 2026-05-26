#pragma once
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace swg {

class piecetable {
  struct impl;

 public:
  piecetable() = default;
  explicit piecetable(std::string_view initbuf) noexcept
      : initbuf_(initbuf),
        piecelist_({piece{.offset = 0, .length = initbuf.length(), .is_original = true}}),
        length_(initbuf.length()) {}
  void insert(size_t pos, std::string_view data);
  void erase(size_t pos, size_t length);
  std::string get(size_t pos, size_t length) const;
  void get_to(size_t pos, std::span<char> out) const;
  size_t length() const noexcept { return length_; }

  // The view currently used as the original buffer for "is_original" pieces.
  // Empty when the table has no externally-owned original buffer (after
  // `detach_initbuf()` or default construction).
  std::string_view initbuf() const noexcept { return initbuf_; }
  // True iff at least one piece still references `initbuf_`. Tests + the host
  // use this to decide whether the external mapping is still load-bearing.
  bool references_initbuf() const noexcept;
  // Copy whatever bytes are still being referenced from `initbuf_` into the
  // owned add buffer, then rewrite the affected pieces so they point at the
  // add buffer instead. After this call `initbuf_` is reset to an empty view
  // and `references_initbuf()` returns false; the table is fully self-owned
  // and the previously-mapped storage may be released safely. No-op when no
  // piece references the external buffer.
  void detach_initbuf();

 private:
  struct piece {
    size_t offset = 0;
    size_t length = 0;
    bool is_original = false;
  };

 private:
  std::vector<piece> piecelist_;
  std::string_view initbuf_;
  std::string addbuf_;
  size_t length_ = 0;
};

}  // namespace swg
