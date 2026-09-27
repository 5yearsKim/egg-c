// https://docs.rs/egg/latest/egg/tutorials/_02_getting_started/index.html
#include <iostream>
#include <stdexcept>
#include <vector>

#include "eggc/all.hpp"

namespace {
using Node = eggc::SymbolLang;

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
  if (report.reason != eggc::StopReason::Saturated) {
    throw std::runtime_error("runner hit a limit");
  }
  std::cout << "Saturated e-graph for " << eggc::to_string(start) << " (root e"
            << graph.find(root) << ", " << graph.class_count() << " classes, "
            << graph.node_count() << " nodes):\n"
            << eggc::to_string(graph);
  auto [cost, best] = eggc::Extractor<Node>(graph).find_best(root);
  if (eggc::to_string(best) != expected || cost != 1) {
    throw std::runtime_error("unexpected extracted result");
  }
  std::cout << eggc::to_string(start) << " -> " << eggc::to_string(best)
            << " (cost " << cost << ")\n";
}
}  // namespace

int main() {
  try {
    optimize("(+ 0 (* 1 a))", "a");
    optimize("(* 0 a)", "0");
    optimize("(+ (* a 1) 0)", "a");
  } catch (const std::exception& error) {
    std::cerr << "Getting-started example failed: " << error.what() << '\n';
    return 1;
  }
}
