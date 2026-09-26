cmake_minimum_required(VERSION 3.16)
project(eggc VERSION 0.1.0 LANGUAGES CXX)
include(CTest)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# User-defined node types instantiate the generic algorithms in their callers.
add_library(eggc INTERFACE)
target_include_directories(eggc INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(eggc INTERFACE cxx_std_20)

if(BUILD_TESTING)
  foreach(test_name language_test concepts_test)
    add_executable(${test_name} tests/${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE eggc)
    set_target_properties(${test_name} PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME ${test_name} COMMAND ${test_name})
  endforeach()
endif()
