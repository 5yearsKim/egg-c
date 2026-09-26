#pragma once
#include <eggc/pattern.hpp>

namespace test_support {
using namespace eggc;
namespace reference {
template <Language L, class A>
  requires AnalysisFor<A, L>
bool enumerate(const EGraph<L, A>& graph, const eggc::Pattern<L>& pattern,
               Id entry, Id eclass, const Substitution& subst,
               const std::function<bool(const Substitution&)>& emit,
               const StopCheck& stop) {
  if (stop && stop()) return false;
  eclass = graph.find(eclass);
  if (const auto* var = std::get_if<Var>(&pattern.nodes[entry])) {
    const auto found = subst.find(var->name);
    if (found != subst.end())
      return graph.find(found->second) != eclass || emit(subst);
    auto bound = subst;
    bound.emplace(var->name, eclass);
    return emit(bound);
  }
  const auto& prototype = std::get<L>(pattern.nodes[entry]);
  for (const L& node : graph.nodes(eclass)) {
    if (stop && stop()) return false;
    if (!prototype.matches(node)) continue;
    // Defensive arity check even if a language's matches() omits it.
    if (prototype.children().size() != node.children().size()) continue;
    std::function<bool(std::size_t, const Substitution&)> visit_children;
    visit_children = [&](std::size_t i, const Substitution& current) {
      if (i == node.children().size()) return emit(current);
      return enumerate(
          graph, pattern, prototype.children()[i], node.children()[i], current,
          [&](const Substitution& next) { return visit_children(i + 1, next); },
          stop);
    };
    if (!visit_children(0, subst)) return false;
  }
  return true;
}
}  // namespace reference
}  // namespace test_support
