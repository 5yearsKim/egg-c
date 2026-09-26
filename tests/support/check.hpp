#pragma once
#include <eggc/all.hpp>
#include <iostream>
#include <stdexcept>
namespace test_support {
using Node = eggc::SymbolLang;
using Graph = eggc::EGraph<Node>;
using Pattern = eggc::Pattern<Node>;
inline void check(bool test, const char* message) {
  if (!test) throw std::runtime_error(message);
}
template <class Error, class F>
void throws(F fn) {
  try {
    fn();
  } catch (const Error&) {
    return;
  }
  throw std::runtime_error("expected exception");
}
template <class... F>
int run_tests(F... tests) {
  try {
    (tests(), ...);
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
}  // namespace test_support
