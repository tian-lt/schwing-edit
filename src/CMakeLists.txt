cmake_minimum_required(VERSION 3.20)

project(schwing VERSION 0.1.0 LANGUAGES CXX)

set(VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(VERSION_PATCH ${PROJECT_VERSION_PATCH})

add_subdirectory(edit)

if (MSVC)
  add_subdirectory(winapp)
endif()

include(CTest)
if (BUILD_TESTING)
  add_subdirectory(ut)
endif()
