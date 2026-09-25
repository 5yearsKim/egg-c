#include "eggc/parser.hpp"
#include "eggc/runner.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace eggc;

namespace {
std::vector<Rewrite> tutorial_rules() {
  return {
      parse_rewrite("div-one", "?x", "(/ ?x 1)"),
      parse_rewrite("unsafe-invert-division", "(/ ?a ?b)", "(/ 1 (/ ?b ?a))"),
      parse_rewrite("simplify-frac", "(/ ?a (/ ?b ?c))",
                    "(/ (* ?a ?c) (* (/ ?b ?c) ?c))"),
      parse_rewrite("cancel-denominator", "(* (/ ?a ?b) ?b)", "?a"),
      parse_rewrite("times-zero", "(* ?a 0)", "0"),
  };
}

bool equivalent(EGraph &graph, const char *lhs, const char *rhs) {
  const Id left = graph.add_expr(parse_expr(lhs));
  const Id right = graph.add_expr(parse_expr(rhs));
  graph.rebuild();
  return graph.find(left) == graph.find(right);
}
} // namespace

int main() {
  const auto rules = tutorial_rules();

  EGraph simplification;
  simplification.add_expr(parse_expr("(/ (* (/ 2 3) (/ 3 2)) 1)"));
  run(simplification, rules);
  const bool simplifies_to_one =
      equivalent(simplification, "(/ (* (/ 2 3) (/ 3 2)) 1)", "1");
  if (!simplifies_to_one)
    throw std::runtime_error("expected equivalence to 1");
  std::cout << "(/ (* (/ 2 3) (/ 3 2)) 1) = 1: yes\n";

  EGraph zero;
  const Id zero_root = zero.add_expr(parse_expr("0"));
  run(zero, rules);
  const bool zero_equals_one =
      !match(zero, parse_pattern("1"), zero_root).empty();
  const bool witness_exists =
      !match(zero, parse_pattern("(* (/ 1 0) 0)"), zero_root).empty();
  if (!zero_equals_one)
    throw std::runtime_error("expected unsafe 0 = 1 equivalence");
  if (!witness_exists)
    throw std::runtime_error("expected division-by-zero witness");
  std::cout << "0 = 1 with unsafe rules: yes\n";
  std::cout << "witness: 0 <- (* (/ 1 0) 0) -> 1\n";
  std::cout << "rules: times-zero, cancel-denominator\n";
}
