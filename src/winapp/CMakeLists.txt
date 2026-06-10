set(RES_RC ${CMAKE_CURRENT_BINARY_DIR}/res.rc)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/res.rc.in ${RES_RC} @ONLY)

add_executable(app WIN32
  winmain.cpp
  sedit.cpp
  ${RES_RC}
)
add_executable(schwing::app ALIAS app)

set_source_files_properties(${RES_RC} PROPERTIES
  COMPILE_FLAGS "/I\"${CMAKE_CURRENT_SOURCE_DIR}\""
)

target_compile_options(app PRIVATE /W4 /WX /permissive- /Zc:__cplusplus)
target_compile_definitions(app PRIVATE
  UNICODE
  _UNICODE
  NOMINMAX
  WIN32_LEAN_AND_MEAN
)

find_package(mio CONFIG QUIET)
find_package(wil CONFIG QUIET)

target_link_libraries(app PRIVATE
  schwing::edit
)
if (TARGET mio::mio)
  target_link_libraries(app PRIVATE mio::mio)
endif()
if (TARGET WIL::WIL)
  target_link_libraries(app PRIVATE WIL::WIL)
endif()
target_link_libraries(app PRIVATE
  Comdlg32
  Shlwapi
  Gdi32
  User32
)

set_target_properties(app PROPERTIES OUTPUT_NAME "schwing-edit")
