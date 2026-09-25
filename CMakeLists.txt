cmake_minimum_required(VERSION 3.16)
project(eggc VERSION 0.1.0 LANGUAGES CXX)

add_library(eggc
    src/egraph.cpp
    src/pattern.cpp
    src/runner.cpp
    src/extract.cpp
)
target_include_directories(eggc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(eggc PUBLIC cxx_std_17)
set_target_properties(eggc PROPERTIES CXX_EXTENSIONS OFF)

add_executable(simplify examples/simplify.cpp)
target_link_libraries(simplify PRIVATE eggc)
