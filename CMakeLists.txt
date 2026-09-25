cmake_minimum_required(VERSION 3.16)
project(eggc VERSION 0.1.0 LANGUAGES CXX)

include(CTest)
option(EGGC_BUILD_EXAMPLES "Build example executables" ON)
option(EGGC_BUILD_BENCHMARKS "Build standalone e-graph benchmarks" OFF)
option(EGGC_ENABLE_EGG_DIFFERENTIAL "Enable optional differential checks against Rust egg" OFF)
option(EGGC_ENABLE_LINT "Add clang-format and clang-tidy targets when available" ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

file(GLOB EGGC_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_library(eggc ${EGGC_SOURCES})
target_include_directories(eggc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(eggc PUBLIC cxx_std_17)
set_target_properties(eggc PROPERTIES CXX_EXTENSIONS OFF)

if(BUILD_TESTING OR EGGC_BUILD_BENCHMARKS)
    add_library(eggc_test_support STATIC tests/support/full_scan.cpp)
    target_link_libraries(eggc_test_support PUBLIC eggc)
endif()

if(EGGC_BUILD_EXAMPLES)
    file(GLOB EGGC_EXAMPLE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/examples/*.cpp")
    foreach(example_source IN LISTS EGGC_EXAMPLE_SOURCES)
        get_filename_component(example_name "${example_source}" NAME_WE)
        add_executable(${example_name} "${example_source}")
        target_link_libraries(${example_name} PRIVATE eggc)
        set_target_properties(${example_name} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples")
    endforeach()
endif()

if(BUILD_TESTING)
    file(GLOB EGGC_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*_test.cpp")
    foreach(test_source IN LISTS EGGC_TEST_SOURCES)
        get_filename_component(test_name "${test_source}" NAME_WE)
        add_executable(${test_name} "${test_source}")
        target_link_libraries(${test_name} PRIVATE eggc_test_support)
        set_target_properties(${test_name} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Testing")
        add_test(NAME ${test_name} COMMAND ${test_name})
    endforeach()
endif()

if(EGGC_BUILD_BENCHMARKS)
    add_executable(eggc_bench benchmarks/eggc_bench.cpp)
    target_link_libraries(eggc_bench PRIVATE eggc_test_support)
endif()

if(EGGC_ENABLE_EGG_DIFFERENTIAL AND BUILD_TESTING)
    find_program(CARGO_EXECUTABLE cargo)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    if(NOT CARGO_EXECUTABLE)
        message(FATAL_ERROR "EGGC_ENABLE_EGG_DIFFERENTIAL requires cargo")
    endif()
    add_executable(eggc_differential tests/differential/cpp_reference.cpp)
    target_link_libraries(eggc_differential PRIVATE eggc)
    add_test(NAME egg_differential
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/differential/check.py
                $<TARGET_FILE:eggc_differential> ${CARGO_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/differential/rust_egg)
    add_test(NAME egg_differential_harness
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/differential/test_harness.py)
endif()

if(EGGC_ENABLE_LINT)
    find_program(CLANG_FORMAT NAMES clang-format clang-format-19 clang-format-18
                 clang-format-17 clang-format-16 clang-format-15)
    find_program(CLANG_TIDY NAMES clang-tidy clang-tidy-19 clang-tidy-18
                 clang-tidy-17 clang-tidy-16 clang-tidy-15)

    file(GLOB_RECURSE EGGC_FORMAT_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.[ch]pp"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/*.[ch]pp"
        "${CMAKE_CURRENT_SOURCE_DIR}/examples/*.[ch]pp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.[ch]pp"
        "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/*.[ch]pp")
    file(GLOB_RECURSE EGGC_TIDY_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/examples/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/*.cpp")

    if(CLANG_FORMAT)
        add_custom_target(format
            COMMAND ${CLANG_FORMAT} -i ${EGGC_FORMAT_SOURCES}
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            COMMENT "Formatting C++ sources")
    else()
        message(STATUS "clang-format not found; the format target is unavailable")
    endif()

    if(CLANG_TIDY)
        add_custom_target(tidy
            COMMAND ${CLANG_TIDY} -p=${CMAKE_BINARY_DIR} ${EGGC_TIDY_SOURCES}
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            COMMENT "Running clang-tidy")
    else()
        message(STATUS "clang-tidy not found; the tidy target is unavailable")
    endif()

    set(EGGC_LINT_TARGETS)
    if(CLANG_FORMAT)
        list(APPEND EGGC_LINT_TARGETS format)
    endif()
    if(CLANG_TIDY)
        list(APPEND EGGC_LINT_TARGETS tidy)
    endif()
    add_custom_target(lint DEPENDS ${EGGC_LINT_TARGETS})
endif()
