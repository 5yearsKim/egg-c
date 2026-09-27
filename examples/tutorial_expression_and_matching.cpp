#include <iostream>

#include "eggc/all.hpp"

int main() {
  // The concrete expression (2 + 3) - 5.
  auto expr = eggc::parse_expr("(- (+ 2 3) 5)");
  eggc::EGraph<eggc::SymbolLang> graph;
  auto root = graph.add_expr(expr);
  graph.rebuild();

  // Search for subtraction whose two operands are equivalent.
  auto pattern = eggc::parse_pattern("(- ?x ?x)");
  auto before = eggc::match(graph, pattern, root);
  std::cout << "Matches before merge: " << before.size()
            << " (2 + 3 and 5 are not yet known to be equal)\n";
  if (!before.empty()) {
    std::cerr << "The graph should not yet know that 2 + 3 equals 5\n";
    return 1;
  }

  // Adding existing expressions retrieves their equivalence classes.
  auto sum = graph.add_expr(eggc::parse_expr("(+ 2 3)"));
  auto five = graph.add_expr(eggc::parse_expr("5"));

  graph.rebuild();
  // Supply a known arithmetic fact; SymbolLang does not compute it for us.
  graph.merge(sum, five);
  graph.rebuild();

  auto after = eggc::match(graph, pattern, root);
  std::cout << "Matches after merge: " << after.size()
            << " (both operands are now known to be equal)\n";
  if (after.size() != 1 || after.front().at("?x") != graph.find(five)) {
    std::cerr << "Expected both operands to match the same value\n";
    return 1;
  }
}
