add_executable(ut
  linetable_tests.cpp
  piecetable_tests.cpp
  plaindoc_tests.cpp
)

target_compile_definitions(ut PRIVATE SWGUT)
find_package(GTest CONFIG REQUIRED)
target_link_libraries(ut PRIVATE
  GTest::gtest
  GTest::gtest_main
  schwing::edit
)

include(GoogleTest)
gtest_discover_tests(ut)
