set(EGGC_TESTS
  language_test
  concepts_test
  symbol_lang_test
  parser_test
  printer_test
  rewrite_text_test
  analysis_hooks_test
  provenance_hooks_test
  extraction_test
  dag_extraction_test
  multipattern_dot_test
  compiled_matcher_test
  runtime_test
  engine_regression_test
)

foreach(test_name IN LISTS EGGC_TESTS)
  add_executable(${test_name} ${test_name}.cpp)
  target_link_libraries(${test_name} PRIVATE eggc)
  set_target_properties(${test_name} PROPERTIES CXX_EXTENSIONS OFF)
  add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

# Compile each public header in its own translation unit to catch hidden include dependencies.
file(GLOB EGGC_PUBLIC_HEADERS CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/include/eggc/*.hpp")
set(EGGC_HEADER_SOURCES)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/headers")
foreach(header IN LISTS EGGC_PUBLIC_HEADERS)
  get_filename_component(header_name "${header}" NAME)
  set(header_source "${CMAKE_CURRENT_BINARY_DIR}/headers/${header_name}.cpp")
  file(WRITE "${header_source}" "#include <eggc/${header_name}>\n")
  list(APPEND EGGC_HEADER_SOURCES "${header_source}")
endforeach()
add_library(header_smoke OBJECT ${EGGC_HEADER_SOURCES})
target_link_libraries(header_smoke PRIVATE eggc)
set_target_properties(header_smoke PROPERTIES CXX_EXTENSIONS OFF)
