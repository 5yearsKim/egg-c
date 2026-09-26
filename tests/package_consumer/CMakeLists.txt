cmake_minimum_required(VERSION 3.16)
project(eggc_package_consumer LANGUAGES CXX)
find_package(eggc CONFIG REQUIRED)
add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE eggc::eggc)

set_target_properties(consumer PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")
