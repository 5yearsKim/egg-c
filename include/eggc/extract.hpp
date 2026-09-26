#pragma once
#include <functional>
#include <optional>

#include "egraph.hpp"

namespace eggc {
// Costs must be deterministic, nondecreasing, and greater than each child's.
// nullopt excludes a node, including when cost arithmetic overflows.
template <Language L>
using CostPolicy = std::function<std::optional<std::size_t>(
    const L&, const std::vector<std::size_t>&)>;
template <Language L>
CostPolicy<L> ast_size_cost();
template <Language L>
CostPolicy<L> ast_depth_cost();

template <Language L, class A = NoAnalysis<L>>
  requires AnalysisFor<A, L>
class Extractor {
 public:
  explicit Extractor(const EGraph<L, A>& graph,
                     CostPolicy<L> cost = ast_size_cost<L>());
  std::size_t best_cost(Id root) const;
  std::pair<std::size_t, RecExpr<L>> find_best(Id root) const;

 private:
  struct Choice {
    std::size_t cost;
    L node;
  };
  const EGraph<L, A>* graph_;
  std::uint64_t revision_;
  std::vector<std::optional<Choice>> choices_;
  void check_graph() const;
  const Choice& choice(Id root) const;
};
}  // namespace eggc
#include "extract.tpp"
