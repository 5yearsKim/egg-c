add_executable(engine_benchmark engine_benchmark.cpp)
target_link_libraries(engine_benchmark PRIVATE eggc)
set_target_properties(engine_benchmark PROPERTIES CXX_EXTENSIONS OFF)

if(BUILD_TESTING)
  add_test(NAME engine_benchmark_smoke COMMAND engine_benchmark all 8 1)
endif()

add_executable(advanced_benchmark advanced_benchmark.cpp)
target_link_libraries(advanced_benchmark PRIVATE eggc)
set_target_properties(advanced_benchmark PROPERTIES CXX_EXTENSIONS OFF)
if(BUILD_TESTING)
  add_test(NAME advanced_benchmark_smoke COMMAND advanced_benchmark all 8 1)
endif()
