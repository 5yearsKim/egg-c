#include <eggc/all.hpp>
#include <iostream>

int main() {
  using Node = eggc::SymbolLang;
  std::vector<eggc::Rewrite<Node>> rules{
      eggc::rewrite("add-zero", "(+ ?x 0)", "?x")};
  eggc::CompiledRules<Node> compiled_rules(rules);
  eggc::EGraph<Node> graph;

  graph.enable_explanations();
  auto root = graph.add_expr(eggc::parse_expr("(+ (+ a 0) 0)"));
  eggc::Runner<Node> runner(graph, compiled_rules);
  eggc::RunOptions slice;
  slice.iteration_limit = 1;
  runner.resume(slice);
  runner.resume(slice);
  if (runner.report().reason != eggc::StopReason::Saturated) {
    std::cerr << "Expected saturation after the second call\n";
    return 1;
  }
  if (!eggc::verify_rewrites(graph, rules)) {
    std::cerr << "Recorded rewrite verification failed\n";
    return 1;
  }

  eggc::Extractor<Node> selected(graph, std::vector{root});
  auto best = selected.find_best(root);
  if (best.first != 1) {
    std::cerr << "Expected a result with one AST node\n";
    return 1;
  }
  std::cout << "(+ (+ a 0) 0) -> " << eggc::to_string(best.second) << " (cost "
            << best.first << ")\n";
  std::cout << "Runner: " << runner.report().iterations
            << " iterations across 2 resume calls (saturated)\n";
  std::cout << "Recorded rewrites verified.\n";
  const auto memory = graph.memory_stats();
  std::cout << "Nodes: " << memory.live_nodes << " active, "
            << memory.allocated_nodes << " retained (includes retired nodes)\n";

  eggc::EGraph<Node> second;
  auto other = second.add_expr(eggc::parse_expr("(+ b 0)"));
  auto report = eggc::run(second, compiled_rules);
  auto second_best =
      eggc::Extractor<Node>(second, std::vector{other}).find_best(other);
  if (report.reason != eggc::StopReason::Saturated || second_best.first != 1) {
    std::cerr
        << "Expected the second graph to saturate with a one-node result\n";
    return 1;
  }
  std::cout << "(+ b 0) -> " << eggc::to_string(second_best.second)
            << " (reused compiled_rules)\n";
}
