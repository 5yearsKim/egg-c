#pragma once
#include <chrono>
#include <cmath>
#include <functional>
#include <unordered_map>

#include "egraph.hpp"

namespace eggc {
struct DagOptions {
  std::size_t state_limit = 100000;
  std::optional<std::chrono::milliseconds> time_limit;
  std::size_t frontier_limit = 100000;
};
enum class DagStopReason { Exhausted, StateLimit, TimeLimit, FrontierLimit };
template <Language L>
struct DagResult {
  std::optional<double> cost;
  RecExpr<L> expression;
  std::size_t explored_states = 0;
  bool optimal = false;
  DagStopReason reason = DagStopReason::Exhausted;
  std::size_t peak_frontier = 0;
};
// Counts selected e-classes once; finite nonnegative local costs. Exact search
// is exponential. State, frontier and time limits return a marked incumbent.
template <Language L, class A = NoAnalysis<L>>
class DagExtractor {
 public:
  using Cost = std::function<std::optional<double>(const L&)>;
  explicit DagExtractor(
      const EGraph<L, A>& graph, Cost cost = [](const L&) { return 1.0; });
  DagResult<L> solve(Id root, const DagOptions& options = {}) const;

 private:
  template <class Stop>
  bool cyclic(const std::unordered_map<Id, std::size_t>& selection,
              const Stop& stop) const;
  const EGraph<L, A>* graph_;
  std::uint64_t revision_;
  Cost cost_;
};
}  // namespace eggc
#include "impl/dag_extract.tpp"
