// std
#include <filesystem>
#include <string>
// gtest
#include <gtest/gtest.h>
// swg
#include "resource.hpp"

namespace swg::ut {

class FreeTypeEnvironment : public ::testing::Environment {
 public:
  void SetUp() override { swg::initialize(); }
  void TearDown() override { swg::uninitialize(); }
};

const std::string& arial_path() {
  static const std::string kPath = []() -> std::string {
    namespace fs = std::filesystem;
    const fs::path candidates[] = {
        "C:\\Windows\\Fonts\\arial.ttf",
        "C:\\Windows\\Fonts\\Arial.ttf",
    };
    for (const auto& p : candidates) {
      if (fs::exists(p)) {
        return p.string();
      }
    }
    return "C:\\Windows\\Fonts\\arial.ttf";
  }();
  return kPath;
}

}  // namespace swg::ut

::testing::Environment* const kFreeTypeEnv =
    ::testing::AddGlobalTestEnvironment(new swg::ut::FreeTypeEnvironment);
