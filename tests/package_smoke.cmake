function(run_checked)
  execute_process(COMMAND ${ARGV} RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "Package consumer command failed: ${ARGV}\n${output}\n${error}")
  endif()
endfunction()
set(prefix "${TEST_BINARY_DIR}/package-smoke/install")
set(consumer_build "${TEST_BINARY_DIR}/package-smoke/consumer")
run_checked("${CMAKE_COMMAND}" --install "${TEST_BINARY_DIR}" --prefix "${prefix}")
run_checked("${CMAKE_COMMAND}" -S "${TEST_SOURCE_DIR}/tests/package_consumer" -B "${consumer_build}"
  "-DCMAKE_PREFIX_PATH=${prefix}" "-DCMAKE_CXX_COMPILER=${TEST_COMPILER}"
  "-DCMAKE_BUILD_TYPE=${TEST_BUILD_TYPE}" "-DCMAKE_CXX_FLAGS=${TEST_FLAGS}")
run_checked("${CMAKE_COMMAND}" --build "${consumer_build}" --parallel 2)
run_checked("${consumer_build}/consumer")
