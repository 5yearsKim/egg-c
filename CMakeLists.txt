cmake_minimum_required(VERSION 3.16)
project(eggc VERSION 0.1.0 LANGUAGES CXX)

include(CTest)

file(GLOB EGGC_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_library(eggc ${EGGC_SOURCES})
target_include_directories(eggc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(eggc PUBLIC cxx_std_17)
set_target_properties(eggc PROPERTIES CXX_EXTENSIONS OFF)

add_executable(simplify examples/simplify.cpp)
target_link_libraries(simplify PRIVATE eggc)

if(BUILD_TESTING)
    add_executable(simplify_test tests/simplify_test.cpp)
    target_link_libraries(simplify_test PRIVATE eggc)
    add_test(NAME simplify_test COMMAND simplify_test)

    add_executable(pattern_test tests/pattern_test.cpp)
    target_link_libraries(pattern_test PRIVATE eggc)
    add_test(NAME pattern_test COMMAND pattern_test)
endif()
