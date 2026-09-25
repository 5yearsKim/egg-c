cmake_minimum_required(VERSION 3.16)
project(eggc VERSION 0.1.0 LANGUAGES CXX)

include(CTest)
option(EGGC_BUILD_BENCHMARKS "Build standalone e-graph benchmarks" OFF)
option(EGGC_ENABLE_EGG_DIFFERENTIAL "Enable optional differential checks against Rust egg" OFF)
option(EGGC_ENABLE_LINT "Add clang-format and clang-tidy targets when available" ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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

    add_executable(runner_test tests/runner_test.cpp)
    target_link_libraries(runner_test PRIVATE eggc)
    add_test(NAME runner_test COMMAND runner_test)

    add_executable(extract_test tests/extract_test.cpp)
    target_link_libraries(extract_test PRIVATE eggc)
    add_test(NAME extract_test COMMAND extract_test)

    add_executable(expr_test tests/expr_test.cpp)
    target_link_libraries(expr_test PRIVATE eggc)
    add_test(NAME expr_test COMMAND expr_test)

    add_executable(analysis_test tests/analysis_test.cpp)
    target_link_libraries(analysis_test PRIVATE eggc)
    add_test(NAME analysis_test COMMAND analysis_test)

    add_executable(egraph_test tests/egraph_test.cpp)
    target_link_libraries(egraph_test PRIVATE eggc)
    add_test(NAME egraph_test COMMAND egraph_test)
endif()

if(EGGC_BUILD_BENCHMARKS)
    add_executable(eggc_bench benchmarks/eggc_bench.cpp)
    target_link_libraries(eggc_bench PRIVATE eggc)
endif()

if(EGGC_ENABLE_EGG_DIFFERENTIAL AND BUILD_TESTING)
    find_program(CARGO_EXECUTABLE cargo)
    if(NOT CARGO_EXECUTABLE)
        message(FATAL_ERROR "EGGC_ENABLE_EGG_DIFFERENTIAL requires cargo")
    endif()
    add_executable(eggc_differential tests/differential/cpp_reference.cpp)
    target_link_libraries(eggc_differential PRIVATE eggc)
    add_test(NAME egg_differential
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/differential/compare.sh
                $<TARGET_FILE:eggc_differential> ${CARGO_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/differential/rust_egg)
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
