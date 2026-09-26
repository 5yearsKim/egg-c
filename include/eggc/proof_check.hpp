#pragma once
#include "rewrite.hpp"
namespace eggc {
// Replay in a fresh graph. Congruence and unconditional named rewrites are
// checked structurally; other equations need explicit external acceptance.
template <Language L, class A, class Trust>
  requires AnalysisFor<A, L>
bool verify_rewrites(const EGraph<L, A>& source,
                     const std::vector<Rewrite<L>>& rules, const Trust& trust);
template <Language L, class A>
  requires AnalysisFor<A, L>
bool verify_rewrites(const EGraph<L, A>& source,
                     const std::vector<Rewrite<L>>& rules);
}  // namespace eggc
#include "impl/proof_check.tpp"
