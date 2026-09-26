#pragma once
#include <functional>
#include <optional>

#include "egraph.hpp"

namespace eggc {
// Costs must be deterministic, nondecreasing, and greater than each child's.
// nullopt excludes a node, including when cost arithmetic overflows.
template <Language L, class Cost = std::size_t>
using CostPolicy =
    std::function<std::optional<Cost>(const L &, const std::vector<Cost> &)>;
template <Language L> CostPolicy<L> ast_size_cost();
template <Language L> CostPolicy<L> ast_depth_cost();

template <Language L, class A = NoAnalysis<L>, class Cost = std::size_t>
  requires AnalysisFor<A, L>
class Extractor {
public:
  explicit Extractor(const EGraph<L, A> &graph, CostPolicy<L, Cost> cost = {});
  Cost best_cost(Id root) const;
  std::pair<Cost, RecExpr<L>> find_best(Id root) const;

private:
  struct Choice {
    Cost cost;
    L node;
  };
  const EGraph<L, A> *graph_;
  std::uint64_t revision_;
  std::vector<std::optional<Choice>> choices_;
  void check_graph() const;
  const Choice &choice(Id root) const;
};
} // namespace eggc
#include "impl/extract.tpp"
