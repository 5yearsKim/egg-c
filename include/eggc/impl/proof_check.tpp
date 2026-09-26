#pragma once
namespace eggc {
template <Language L, class A, class Trust>
  requires AnalysisFor<A, L>
bool verify_rewrites(const EGraph<L, A>& source,
                     const std::vector<Rewrite<L>>& rules, const Trust& trust) {
  const auto& terms = source.explanation_terms();
  const auto& steps = source.explanation_steps();
  EGraph<L> replay;
  std::vector<Id> mapping;
  mapping.reserve(terms.size());
  for (auto node : terms) {
    for (Id& child : node.children_mut()) {
      if (child >= mapping.size()) return false;
      child = mapping[child];
    }
    mapping.push_back(replay.add(std::move(node)));
  }
  replay.rebuild();
  for (const auto& rule : rules) validate_rewrite(rule);
  std::vector<std::optional<CompiledPattern<L>>> searchers(rules.size());
  std::vector<std::optional<CompiledReplacement<L>>> replacements(rules.size());
  MatcherWorkspace workspace;
  for (const auto& step : steps) {
    if (step.lhs >= mapping.size() || step.rhs >= mapping.size()) return false;
    const Id lhs = mapping[step.lhs], rhs = mapping[step.rhs];
    for (auto [a, b] : step.justification.premises)
      if (a >= mapping.size() || b >= mapping.size() ||
          replay.find(mapping[a]) != replay.find(mapping[b]))
        return false;
    bool valid = false;
    if (step.justification.kind == UnionKind::Congruence) {
      valid = replay.find(lhs) == replay.find(rhs);
    } else if (step.justification.kind == UnionKind::Rewrite) {
      for (std::size_t i = 0; i < rules.size(); ++i) {
        const auto& rule = rules[i];
        if (rule.name != step.justification.name || rule.custom_search ||
            rule.condition)
          continue;
        if (!searchers[i]) {
          searchers[i].emplace(*rule.lhs);
          replacements[i].emplace(*rule.rhs, searchers[i]->variables());
        }
        searchers[i]->search(
            replay, lhs, workspace, [&](const std::vector<Id>& bindings) {
              auto found = replacements[i]->lookup(replay, bindings);
              valid = found && replay.find(*found) == replay.find(rhs);
              return !valid;
            });
        if (valid) break;
      }
      if (!valid) valid = trust(step);
    } else
      valid = trust(step);
    if (!valid) return false;
    replay.merge(lhs, rhs);
    replay.rebuild();
  }
  return true;
}
template <Language L, class A>
  requires AnalysisFor<A, L>
bool verify_rewrites(const EGraph<L, A>& source,
                     const std::vector<Rewrite<L>>& rules) {
  return verify_rewrites(source, rules, [](const ProofStep&) { return false; });
}
}  // namespace eggc
