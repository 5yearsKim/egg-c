#include <eggc/all.hpp>
#include <iostream>

int main() {
  using Node = eggc::SymbolLang;
  eggc::EGraph<Node> graph;
  graph.enable_explanations();
  auto original = graph.add_expr(eggc::parse_expr("(+ a 0)"));
  auto a = graph.add(Node::leaf("a"));
  auto report = eggc::run(
      graph, std::vector{eggc::rewrite("add-zero", "(+ ?x 0)", "?x")});
  if (report.reason != eggc::StopReason::Saturated) return 1;
  auto steps = graph.explain_equivalence(original, a);
  if (steps.empty()) return 1;
  std::cout << "Equality established by: " << steps.front().justification.name
            << '\n';

  auto f = graph.add_expr(eggc::parse_expr("(f x x x)"));
  auto g = graph.add_expr(eggc::parse_expr("(g (g x))"));
  graph.merge(f, g);
  graph.rebuild();
  auto tree = eggc::Extractor<Node>(graph).find_best(f);
  auto dag = eggc::DagExtractor<Node>(graph).solve(f);
  if (!dag.optimal || !dag.cost) return 1;
  std::cout << "Best tree: " << eggc::to_string(tree.second) << '\n';
  std::cout << "Best DAG: " << eggc::to_string(dag.expression) << '\n';

  graph.add_expr(eggc::parse_expr("(left p p)"));
  graph.add_expr(eggc::parse_expr("(right p p)"));
  graph.rebuild();
  eggc::MultiPattern<Node> joined(
      {{"?left", eggc::parse_pattern("(left ?x ?y)")},
       {"?right", eggc::parse_pattern("(right ?x ?y)")}});
  auto matches = joined.match(graph);
  if (matches.size() != 1) return 1;
  std::cout << "Joined matches: " << matches.size() << '\n';
  graph.check_invariants();
  std::cout << eggc::to_dot(graph) << '\n';
  return graph.lookup_expr(eggc::parse_expr("(left p p)")) &&
                 !eggc::to_dot(graph).empty()
             ? 0
             : 1;
}
