set(EGGC_TESTS
  language_test
  concepts_test
  symbol_lang_test
  parser_test
  printer_test
  rewrite_text_test
)

foreach(test_name IN LISTS EGGC_TESTS)
  add_executable(${test_name} ${test_name}.cpp)
  target_link_libraries(${test_name} PRIVATE eggc)
  set_target_properties(${test_name} PROPERTIES CXX_EXTENSIONS OFF)
  add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
