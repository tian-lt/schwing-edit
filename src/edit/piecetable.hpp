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
        piecelist_({piece{.offset = 0, .length = initbuf.length(), .is_original = true}}) {}
  void insert(size_t pos, std::string_view data);
  void erase(size_t pos, size_t length);
  std::string get(size_t pos, size_t length) const;
  void get_to(size_t pos, std::span<char> out) const;
  size_t length() const;

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
};

}  // namespace swg
