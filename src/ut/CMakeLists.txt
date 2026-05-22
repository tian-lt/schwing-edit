add_executable(ut
  font_env.cpp
  fontengine_tests.cpp
  glyphatlas_tests.cpp
  host_doc_ops_tests.cpp
  host_find_tests.cpp
  host_goto_tests.cpp
  host_motion_tests.cpp
  host_selection_tests.cpp
  host_undo_tests.cpp
  linetable_api_tests.cpp
  linetable_perf_tests.cpp
  linetable_tests.cpp
  piecetable_tests.cpp
  textlayout_tests.cpp
  textshaper_tests.cpp
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
