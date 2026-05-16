add_library(edit
  fontengine.cpp
  linetable.cpp
  piecetable.cpp
  plaindoc.cpp
  resource.cpp
)

target_include_directories(edit PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(edit PUBLIC cxx_std_23)
find_package(harfbuzz CONFIG REQUIRED)
find_package(Freetype REQUIRED)
target_link_libraries(edit PUBLIC
  Freetype::Freetype
  harfbuzz::harfbuzz
)

if (MSVC)
  target_compile_options(edit PRIVATE /W4 /WX)
else()
  target_compile_options(edit PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_library(schwing::edit ALIAS edit)
