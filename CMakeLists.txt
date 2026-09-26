cmake_minimum_required(VERSION 3.16)
project(eggc VERSION 0.1.0 LANGUAGES CXX)
include(CTest)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# User-defined node types instantiate the generic algorithms in their callers.
add_library(eggc INTERFACE)
target_include_directories(eggc INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(eggc INTERFACE cxx_std_20)

option(EGGC_BUILD_EXAMPLES "Build egg-c examples" ON)
if(EGGC_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
