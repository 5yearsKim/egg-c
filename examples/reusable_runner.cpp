#include <eggc/all.hpp>
#include <iostream>

int main() {
  using Node = eggc::SymbolLang;
  std::vector<eggc::Rewrite<Node>> rules{
      eggc::rewrite("add-zero", "(+ ?x 0)", "?x")};
  eggc::CompiledRules<Node> compiled(rules);
  eggc::EGraph<Node> graph;
  graph.enable_explanations();
  auto root = graph.add_expr(eggc::parse_expr("(+ (+ a 0) 0)"));
  eggc::Runner<Node> runner(graph, compiled);
  eggc::RunOptions slice;
  slice.iteration_limit = 1;
  runner.resume(slice);
  runner.resume(slice);
  if (runner.report().reason != eggc::StopReason::Saturated) return 1;
  if (!eggc::verify_rewrites(graph, rules)) return 1;
  eggc::Extractor<Node> selected(graph, std::vector{root});
  auto best = selected.find_best(root);
  if (best.first != 1) return 1;
  std::cout << "Optimized: " << eggc::to_string(best.second) << '\n';
  std::cout << "Iterations across slices: " << runner.report().iterations
            << '\n';
  const auto memory = graph.memory_stats();
  std::cout << "Live nodes: " << memory.live_nodes
            << ", retained arena nodes: " << memory.allocated_nodes << '\n';

  eggc::EGraph<Node> second;
  auto other = second.add_expr(eggc::parse_expr("(+ b 0)"));
  eggc::run(second, compiled);
  return eggc::Extractor<Node>(second, std::vector{other}).best_cost(other) == 1
             ? 0
             : 1;
}
