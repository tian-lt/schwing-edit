// gtest
#include <gtest/gtest.h>

// schwing
#include "piecetable.hpp"

namespace swg::ut {

TEST(piecetable_tests, insert) {
  {
    piecetable pt;
    pt.insert(0, "hello");
    pt.insert(5, "world");
    pt.insert(10, "!!!");
  }
}

}  // namespace swg::ut
