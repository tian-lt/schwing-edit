#pragma once

#include <flat_map>
#include <list>
#include <memory_resource>
#include <type_traits>

namespace swg {

template <class K, class V>
class lrucache {
 public:
  explicit lrucache(size_t capacity) : capacity_(capacity) {}
  void set(K key, V value) {
    if (auto map_it = map_.find(key); map_it != map_.end()) {
      map_it->second->second = std::move(value);
      list_.splice(list_.begin(), list_, map_it->second);
      return;
    }
    if (capacity_ == 0) {
      return;
    }
    if (list_.size() >= capacity_) {
      auto it = list_.back();
      map_.erase(it.first);
      list_.pop_back();
    }
    list_.emplace_front(std::move(key), std::move(value));
    map_[list_.front().first] = list_.begin();
  }
  decltype(auto) get(this const auto& self, const K& key) {
    auto& it = self.map_.at(key);
    self.list_.splice(self.list_.begin(), self.list_, it);
    return (it->second);
  }
  bool contains(const K& key) const { return map_.contains(key); }
  void erase(const K& key) {
    auto it = map_.find(key);
    if (it == map_.end()) {
      return;
    }
    list_.erase(it->second);
    map_.erase(it);
  }

 private:
  using list_type = std::pmr::list<std::pair<K, V>>;
  std::pmr::unsynchronized_pool_resource pool_;
  std::flat_map<K, typename list_type::iterator> map_{};
  mutable list_type list_{std::pmr::polymorphic_allocator<typename list_type::value_type>{&pool_}};
  size_t capacity_;
};

}  // namespace swg
