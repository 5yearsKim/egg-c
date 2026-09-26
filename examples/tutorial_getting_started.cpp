// https://docs.rs/egg/latest/egg/tutorials/_02_getting_started/index.html
#include <iostream>
#include <stdexcept>
#include <vector>

#include "eggc/all.hpp"

namespace {
using Node = eggc::SymbolLang;
void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

void expressions_and_matching() {
  auto expr = eggc::parse_expr("(foo a b)");
  std::cout << "Expression: " << eggc::to_string(expr) << '\n';

  eggc::EGraph<Node> graph;
  auto a = graph.add(Node::leaf("a"));
  auto b = graph.add(Node::leaf("b"));
  auto foo = graph.add(Node::node("foo", {a, b}));
  check(graph.find(foo) == graph.find(graph.add_expr(expr)),
        "duplicate expression differs");
  graph.rebuild();

  // Both occurrences of ?x must refer to the same equivalence class.
  auto pattern = eggc::parse_pattern("(foo ?x ?x)");
  auto before = eggc::match(graph, pattern, foo);
  check(before.empty(), "distinct children should not match");
  graph.merge(a, b);
  graph.rebuild();
  auto after = eggc::match(graph, pattern, foo);
  check(after.size() == 1 && after.front().at("?x") == graph.find(a),
        "missing repeated-variable match");
  std::cout << "Matches for (foo ?x ?x): " << before.size() << " before merge, "
            << after.size() << " after merge\n";
}

void optimize(std::string_view input, std::string_view expected) {
  using eggc::rewrite;
  std::vector rules{
      rewrite("commute-add", "(+ ?x ?y)", "(+ ?y ?x)"),
      rewrite("commute-mul", "(* ?x ?y)", "(* ?y ?x)"),
      rewrite("add-0", "(+ ?x 0)", "?x"),
      rewrite("mul-0", "(* ?x 0)", "0"),
      rewrite("mul-1", "(* ?x 1)", "?x"),
  };
  auto start = eggc::parse_expr(input);
  eggc::EGraph<Node> graph;
  auto root = graph.add_expr(start);
  auto report = eggc::run(graph, rules);
  check(report.reason == eggc::StopReason::Saturated, "runner hit a limit");
  std::cout << "Saturated e-graph for " << eggc::to_string(start) << " (root e"
            << graph.find(root) << ", " << graph.class_count() << " classes, "
            << graph.node_count() << " nodes):\n"
            << eggc::to_string(graph);
  auto [cost, best] = eggc::Extractor<Node>(graph).find_best(root);
  check(eggc::to_string(best) == expected && cost == 1,
        "unexpected extracted result");
  std::cout << eggc::to_string(start) << " -> " << eggc::to_string(best)
            << " (cost " << cost << ")\n";
}
} // namespace

int main() {
  try {
    expressions_and_matching();
    optimize("(+ 0 (* 1 a))", "a");
    optimize("(* 0 a)", "0");
    optimize("(+ (* a 1) 0)", "a");
  } catch (const std::exception &error) {
    std::cerr << "Getting-started example failed: " << error.what() << '\n';
    return 1;
  }
}
