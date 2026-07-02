// gtest
#include <gtest/gtest.h>

// swg
#include "lrucache.hpp"

namespace swg::ut {

TEST(lrucache_tests, get) {
  lrucache<int, std::string> cache{3};
  cache.set(0, "zero");
  EXPECT_EQ(cache.get(0), "zero");
  const auto& ccache = cache;
  EXPECT_EQ(ccache.get(0), "zero");
  cache.get(0) = "ZERO";
  EXPECT_EQ(cache.get(0), "ZERO");
  cache.set(1, "one");
  EXPECT_EQ(cache.get(0), "ZERO");
  EXPECT_EQ(cache.get(1), "one");
  EXPECT_THROW(cache.get(2), std::out_of_range);
}

TEST(lrucache_tests, evicts_least_recently_used_entry) {
  lrucache<int, std::string> cache{2};
  cache.set(1, "one");
  cache.set(2, "two");
  EXPECT_EQ(cache.get(1), "one");
  cache.set(3, "three");
  EXPECT_EQ(cache.get(1), "one");
  EXPECT_EQ(cache.get(3), "three");
  EXPECT_THROW(cache.get(2), std::out_of_range);
}

TEST(lrucache_tests, const_get_refreshes_recency) {
  lrucache<int, std::string> cache{2};
  cache.set(1, "one");
  cache.set(2, "two");
  const auto& ccache = cache;
  EXPECT_EQ(ccache.get(1), "one");
  cache.set(3, "three");
  EXPECT_EQ(cache.get(1), "one");
  EXPECT_EQ(cache.get(3), "three");
  EXPECT_THROW(cache.get(2), std::out_of_range);
}

TEST(lrucache_tests, set_replaces_existing_key_without_evicting_others) {
  lrucache<int, std::string> cache{2};
  cache.set(1, "one");
  cache.set(2, "two");
  EXPECT_EQ(cache.get(1), "one");
  cache.set(1, "ONE");
  EXPECT_EQ(cache.get(2), "two");
  EXPECT_EQ(cache.get(1), "ONE");
  cache.set(3, "three");
  EXPECT_EQ(cache.get(1), "ONE");
  EXPECT_EQ(cache.get(3), "three");
  EXPECT_THROW(cache.get(2), std::out_of_range);
}

TEST(lrucache_tests, zero_capacity_ignores_insertions) {
  lrucache<int, std::string> cache{0};
  cache.set(1, "one");
  EXPECT_THROW(cache.get(1), std::out_of_range);
}

}  // namespace swg::ut
