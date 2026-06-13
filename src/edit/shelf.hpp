#pragma once

// std
#include <cassert>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace swg {

template <class V>
  requires(!std::is_reference_v<V>)
struct shelfcell {
  V payload = {};
  unsigned x = 0;
  unsigned y = 0;
  unsigned w = 0;
  unsigned h = 0;
};

template <class K, class V, class H = std::hash<K>>
class shelfset {
  static constexpr unsigned padding = 1;

 public:
  explicit shelfset(unsigned width, unsigned height) : width_(width), height_(height) {}
  void add_shelf() { shelves_.emplace_back(); }
  size_t shelf_count() const noexcept { return shelves_.size(); }
  const shelfcell<V>* try_get_view(const K& key) const {
    auto iter = map_.find(key);
    if (iter != map_.end()) {
      return &iter->second;
    }
    return nullptr;
  }
  const shelfcell<V>* try_put(const K& key, V value, unsigned w, unsigned h) {
    assert(!map_.contains(key));
    if (w + padding > width_ || h + padding > height_) {
      return nullptr;
    }
    for (auto& s : shelves_) {
      for (auto& r : s.rows) {
        if (h <= r.h && r.h <= h * 3 / 2 && r.cursor_x + w + padding <= width_) {
          auto [it, _] = map_.emplace(key, shelfcell<V>{
                                               .payload = std::move(value),
                                               .x = r.cursor_x,
                                               .y = r.y,
                                               .w = w,
                                               .h = h,
                                           });
          r.cursor_x += w + padding;
          return &it->second;
        }
      }
      if (s.cursor_y + h + padding <= height_) {
        s.rows.push_back({.y = s.cursor_y, .h = h, .cursor_x = w + padding});
        auto [it, _] = map_.emplace(key, shelfcell<V>{
                                             .payload = std::move(value),
                                             .x = 0,
                                             .y = s.cursor_y,
                                             .w = w,
                                             .h = h,
                                         });
        s.cursor_y += h + padding;
        return &it->second;
      }
    }
    return nullptr;
  }

 private:
  struct row {
    unsigned y = 0;
    unsigned h = 0;
    unsigned cursor_x = 0;
  };
  struct shelf {
    std::vector<row> rows;
    unsigned cursor_y = 0;
  };
  std::unordered_map<K, shelfcell<V>, H> map_;
  std::vector<shelf> shelves_;
  unsigned width_ = 0;
  unsigned height_ = 0;
};

}  // namespace swg
