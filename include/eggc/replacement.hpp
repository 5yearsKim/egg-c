#pragma once
#include "pattern_types.hpp"
namespace eggc {
// Linear bottom-up construction/lookup. Shared DAG entries are never unfolded.
template <Language L>
class CompiledReplacement {
 public:
  explicit CompiledReplacement(Pattern<L> pattern,
                               std::vector<std::string> variables = {});
  const std::vector<std::string>& variables() const noexcept {
    return variables_;
  }
  const Pattern<L>& pattern() const noexcept { return pattern_; }
  Substitution substitution(const std::vector<Id>& bindings) const;
  template <class A>
  Id instantiate(EGraph<L, A>& graph, const std::vector<Id>& bindings) const;
  template <class A>
  Id instantiate(EGraph<L, A>& graph, const std::vector<Id>& bindings,
                 std::vector<Id>& scratch) const;
  template <class A>
  std::optional<Id> lookup(const EGraph<L, A>& graph,
                           const std::vector<Id>& bindings) const;

 protected:
  Pattern<L> pattern_;
  std::vector<std::string> variables_;
  std::vector<Id> slots_;
};
template <Language L, class A>
  requires AnalysisFor<A, L>
Id instantiate(EGraph<L, A>& graph, const Pattern<L>& pattern,
               const Substitution& substitution);
}  // namespace eggc
#include "impl/replacement.tpp"
