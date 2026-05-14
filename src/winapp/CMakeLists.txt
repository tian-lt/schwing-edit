add_executable(app WIN32
  sedit.cpp
  winmain.cpp
)
find_package(wil CONFIG REQUIRED)
target_link_libraries(app PRIVATE
  edit
  WIL::WIL
)

add_executable(schwing::app ALIAS app)
