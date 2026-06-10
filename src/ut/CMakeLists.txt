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

if (MSVC)
  target_compile_options(ut PRIVATE /W4 /WX /permissive- /Zc:__cplusplus)
else()
  target_compile_options(ut PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

include(GoogleTest)
gtest_discover_tests(ut)
