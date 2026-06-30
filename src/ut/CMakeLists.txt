add_executable(ut
  host_tests.cpp
  itemizer_tests.cpp
  linetable_tests.cpp
  piecetable_tests.cpp
  plaindoc_tests.cpp
  shelf_tests.cpp
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
