add_executable(ut
  linetable_tests.cpp
  piecetable_tests.cpp
)

find_package(GTest CONFIG REQUIRED)
target_link_libraries(ut PRIVATE
  GTest::gtest
  GTest::gtest_main
  schwing::edit
)

include(GoogleTest)
gtest_discover_tests(ut)
