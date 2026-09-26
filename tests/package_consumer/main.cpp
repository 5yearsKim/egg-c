#include <eggc/all.hpp>
int main() {
  eggc::EGraph<eggc::SymbolLang> graph;
  auto root = graph.add_expr(eggc::parse_expr("(+ a 0)"));
  auto report =
      eggc::run(graph, std::vector{eggc::rewrite("zero", "(+ ?x 0)", "?x")});
  auto [cost, expression] =
      eggc::Extractor<eggc::SymbolLang>(graph).find_best(root);
  if (report.reason != eggc::StopReason::Saturated || cost != 1 ||
      eggc::to_string(expression) != "a")
    return 1;
  auto dag = eggc::DagExtractor<eggc::SymbolLang>(graph).solve(root);
  return !dag.optimal || dag.cost != 1 || eggc::to_dot(graph).empty();
}
