configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/res.rc.in
  ${CMAKE_BINARY_DIR}/res.rc
  @ONLY
)

add_executable(app WIN32
  sedit.cpp
  glcaret.cpp
  theme.cpp
  winmain.cpp
  ${CMAKE_BINARY_DIR}/res.rc
)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(app PRIVATE
  UNICODE
  _UNICODE
)
find_package(wil CONFIG REQUIRED)
target_link_libraries(app PRIVATE
  edit
  WIL::WIL
  Dwmapi
  Comdlg32
)

add_executable(schwing::app ALIAS app)
