#pragma once
namespace eggc {
template <Language L, class A, class Callback>
  requires AnalysisFor<A, L>
bool search_matches(const EGraph<L, A>& graph, const Pattern<L>& pattern,
                    Id eclass, Callback&& on_match, const StopCheck& stop) {
  const CompiledPattern<L> compiled(pattern);
  return compiled.search(
      graph, eclass,
      [&](const std::vector<Id>& bindings) {
        return on_match(compiled.substitution(bindings));
      },
      stop);
}
template <Language L, class A>
  requires AnalysisFor<A, L>
std::vector<Substitution> match(const EGraph<L, A>& graph,
                                const Pattern<L>& pattern, Id eclass) {
  std::vector<Substitution> result;
  search_matches(graph, pattern, eclass, [&](const Substitution& subst) {
    result.push_back(subst);
    return true;
  });
  return result;
}
}  // namespace eggc
