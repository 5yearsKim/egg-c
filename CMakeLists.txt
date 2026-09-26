cmake_minimum_required(VERSION 3.16)
project(eggc VERSION 0.1.0 LANGUAGES CXX)
include(CTest)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# User-defined node types instantiate the generic algorithms in their callers.
add_library(eggc INTERFACE)
add_library(eggc::eggc ALIAS eggc)
target_include_directories(eggc INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(eggc INTERFACE cxx_std_20)

option(EGGC_BUILD_EXAMPLES "Build egg-c examples" ON)
if(EGGC_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()

option(EGGC_BUILD_BENCHMARKS "Build egg-c engine benchmarks" OFF)
if(EGGC_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()

install(TARGETS eggc EXPORT eggcTargets)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  FILES_MATCHING PATTERN "*.hpp" PATTERN "*.tpp")
install(EXPORT eggcTargets NAMESPACE eggc:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/eggc)
configure_package_config_file(cmake/eggcConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/eggcConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/eggc)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/eggcConfigVersion.cmake
  VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/eggcConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/eggcConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/eggc)
if(BUILD_TESTING)
  add_test(NAME package_consumer COMMAND ${CMAKE_COMMAND}
    -DTEST_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
    -DTEST_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
    -DTEST_COMPILER=${CMAKE_CXX_COMPILER}
    -DTEST_BUILD_TYPE=${CMAKE_BUILD_TYPE}
    -DTEST_CONFIG=$<CONFIG>
    -DTEST_EXEC_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}
    "-DTEST_FLAGS=${CMAKE_CXX_FLAGS}"
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/package_smoke.cmake)
endif()
