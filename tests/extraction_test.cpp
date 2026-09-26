#include <limits>

#include "support/check.hpp"

namespace {
using namespace test_support;
void extraction_costs_and_dags() {
  Graph graph;
  auto f = graph.add_expr(eggc::parse_expr("(f x x x)"));
  auto g = graph.add_expr(eggc::parse_expr("(g (g x))"));
  graph.merge(f, g);
  graph.rebuild();
  check(eggc::to_string(eggc::Extractor<Node>(graph).find_best(f).second) ==
            "(g (g x))",
        "tree extraction changed objective");
  auto dag = eggc::DagExtractor<Node>(graph).solve(f);
  check(dag.optimal && dag.cost == 2 && dag.expression.nodes.size() == 2 &&
            eggc::to_string(dag.expression) == "(f x x x)",
        "DAG extraction double-counted shared children");
  eggc::DagOptions limit;
  limit.state_limit = 0;
  auto limited = eggc::DagExtractor<Node>(graph).solve(f, limit);
  check(!limited.optimal && !limited.cost && limited.explored_states == 0,
        "bounded DAG search claimed optimality");
  limit.state_limit = 3;
  auto incumbent = eggc::DagExtractor<Node>(graph).solve(f, limit);
  check(!incumbent.optimal && incumbent.cost == 2,
        "bounded DAG search lost incumbent");

  eggc::CostPolicy<Node, double> latency =
      [](const Node& n,
         const std::vector<double>& children) -> std::optional<double> {
    double result = n.op == "f" ? 100 : 0.5;
    for (double cost : children) result += cost;
    return result;
  };
  auto floating =
      eggc::Extractor<Node, eggc::NoAnalysis<Node>, double>(graph, latency)
          .find_best(f);
  check(
      floating.first == 1.5 && eggc::to_string(floating.second) == "(g (g x))",
      "floating-point extraction failed");
  using Pair = std::pair<std::size_t, std::size_t>;
  eggc::CostPolicy<Node, Pair> lex =
      [](const Node&,
         const std::vector<Pair>& children) -> std::optional<Pair> {
    Pair result{1, 1};
    for (auto c : children) {
      result.first += c.first;
      result.second += c.second;
    }
    return result;
  };
  check(eggc::Extractor<Node, eggc::NoAnalysis<Node>, Pair>(graph, lex)
                .best_cost(f) == Pair{3, 3},
        "lexicographic extraction failed");
  throws<std::invalid_argument>([&] {
    eggc::Extractor<Node, eggc::NoAnalysis<Node>, double> invalid(
        graph,
        [](const Node&, const std::vector<double>&) -> std::optional<double> {
          return std::numeric_limits<double>::quiet_NaN();
        });
  });
  throws<std::invalid_argument>([&] {
    eggc::DagExtractor<Node>(graph, [](const Node&) { return -1.0; }).solve(f);
  });
  auto cycle = graph.add(Node::node("cycle", {f}));
  graph.merge(cycle, f);
  graph.rebuild();
  auto finite = eggc::DagExtractor<Node>(graph).solve(f);
  check(finite.optimal && finite.cost == 2, "DAG extraction selected a cycle");
  auto stale = eggc::DagExtractor<Node>(graph);
  graph.add(Node::leaf("later"));
  throws<std::logic_error>([&] { stale.solve(f); });
}

}  // namespace
int main() { return test_support::run_tests(extraction_costs_and_dags); }
