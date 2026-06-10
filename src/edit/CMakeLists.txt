add_library(edit
  linetable.cpp
  piecetable.cpp
  plaindoc.cpp
)

target_include_directories(edit PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(edit PUBLIC cxx_std_23)

if (MSVC)
  target_compile_options(edit PRIVATE /W4 /WX /permissive- /Zc:__cplusplus)
else()
  target_compile_options(edit PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_library(schwing::edit ALIAS edit)
