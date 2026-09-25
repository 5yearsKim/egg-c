#include "eggc/extract.hpp"
#include "eggc/parser.hpp"
#include "eggc/runner.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace eggc;

int main() {
  // A RecExpr is a plain expression; an EGraph stores equivalent expressions.
  const RecExpr expr = parse_expr("(foo a b)");
  std::cout << "expression: " << to_string(expr) << '\n';

  EGraph graph;
  const Id a = graph.add("a");
  const Id b = graph.add("b");
  const Id foo = graph.add("foo", {a, b});
  const Id foo_again = graph.add_expr(expr);
  graph.rebuild();
  if (graph.find(foo) != graph.find(foo_again))
    throw std::runtime_error("adding the same expression changed its class");
  std::cout << "same expression shares a class: yes\n";

  // Repeating ?x requires both children to belong to the same e-class.
  const Pattern repeated = parse_pattern("(foo ?x ?x)");
  const bool before = !match(graph, repeated, foo).empty();
  graph.merge(a, b);
  graph.rebuild();
  const bool after = !match(graph, repeated, foo).empty();
  if (before || !after)
    throw std::runtime_error("unexpected pattern match");
  std::cout << "(foo ?x ?x) before merge: no; after merge: yes\n";

  EGraph optimizer;
  const Id root = optimizer.add_expr(parse_expr("(+ 0 (* 1 a))"));
  const std::vector<Rewrite> rules{
      parse_rewrite("commute-add", "(+ ?x ?y)", "(+ ?y ?x)"),
      parse_rewrite("commute-mul", "(* ?x ?y)", "(* ?y ?x)"),
      parse_rewrite("add-0", "(+ ?x 0)", "?x"),
      parse_rewrite("mul-0", "(* ?x 0)", "0"),
      parse_rewrite("mul-1", "(* ?x 1)", "?x"),
  };
  run(optimizer, rules);
  const auto [cost, best] = Extractor(optimizer).find_best_rec_expr(root);
  if (to_string(best) != "a" || cost != 1)
    throw std::runtime_error("unexpected extracted expression");
  std::cout << "best: " << to_string(best) << " (AST size " << cost << ")\n";
}
