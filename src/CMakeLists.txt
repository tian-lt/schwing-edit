cmake_minimum_required(VERSION 3.20)

project(schwing VERSION 0.0.1 LANGUAGES CXX)

add_subdirectory(edit)

include(CTest)
if (BUILD_TESTING)
  add_subdirectory(ut)
endif()

