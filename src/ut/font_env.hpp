#pragma once
#include <string>

namespace swg::ut {

// Path to the Arial font used by the test suite. Resolves to the first
// existing file under C:\Windows\Fonts at process startup. Tests that depend
// on the font should GTEST_SKIP() if std::filesystem::exists returns false on
// this path, so the suite stays green on machines without the font.
const std::string& arial_path();

}  // namespace swg::ut
