#pragma once
#include <stdexcept>
#include <string>
#include <variant>

#include "language.hpp"

namespace eggc {
template <Language L, class A>
class EGraph;
enum class AnalysisMerge { Unchanged, Changed, Conflict };
class AnalysisConflict : public std::runtime_error {
 public:
  explicit AnalysisConflict(const std::string& message)
      : std::runtime_error(message) {}
};
// Checked after A is complete. The graph reference lets analyses declare
// make() using their own EGraph<L, A> before their definition is finished.
template <class A, class L>
concept AnalysisFor =
    Language<L> && std::move_constructible<A> &&
    requires(A& analysis, const EGraph<L, A>& graph, const L& node,
             typename A::Data& into, const typename A::Data& from) {
      requires std::copyable<typename A::Data>;
      { analysis.make(graph, node) } -> std::same_as<typename A::Data>;
      { analysis.merge(into, from) } -> std::same_as<AnalysisMerge>;
    };
// Analysis supplies Data, make(graph, node), and merge(into, from). merge must
// be associative, commutative, and idempotent. Conflicts reject a union before
// graph mutation. Facts describe all representatives of an e-class. make()
// must be deterministic from the node and its child facts, and monotone as
// those facts grow. The domain must converge (for example, have finite height).
// Rebuilding a clean graph performs no analysis work; external state changes
// are not a way to invalidate facts. Conflict rejection is local to a union,
// not a transaction that rolls back an entire rewrite or rebuild.
template <Language L>
struct NoAnalysis {
  using Data = std::monostate;
  Data make(const EGraph<L, NoAnalysis>&, const L&) const { return {}; }
  AnalysisMerge merge(Data&, const Data&) const {
    return AnalysisMerge::Unchanged;
  }
};
}  // namespace eggc
