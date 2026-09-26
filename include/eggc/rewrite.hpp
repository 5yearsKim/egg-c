#pragma once
#include <algorithm>
#include <optional>

#include "pattern.hpp"

namespace eggc {
template <Language L, class A = NoAnalysis<L>>
  requires AnalysisFor<A, L>
struct Condition {
  std::string name;
  std::vector<std::string> required_variables;
  std::function<bool(const EGraph<L, A> &, Id, const Substitution &)> check;
};
// Search occurs on a clean snapshot. Capture attrs/nodes by value in apply;
// application may use find(), add(), and merge(), but cannot query dirty nodes.
// Returning nullopt means no replacement; the runner merges a returned ID.
template <Language L, class A = NoAnalysis<L>>
  requires AnalysisFor<A, L>
struct Application {
  Id target;
  std::function<std::optional<Id>(EGraph<L, A> &)> apply;
  // Custom searchers can emit rejected structural matches without an applier.
  // This lets the runner count them toward match budgets and condition stats.
  std::optional<bool> condition_result = std::nullopt;
};
template <Language L, class A = NoAnalysis<L>>
  requires AnalysisFor<A, L>
struct Rewrite {
  using Sink = std::function<bool(Application<L, A>)>;
  // Return false on cancellation. Honor sink=false and the cooperative stop.
  using Searcher = std::function<bool(const EGraph<L, A> &, const Sink &,
                                      const StopCheck &)>;
  std::string name;
  std::optional<Pattern<L>> lhs;
  std::optional<Pattern<L>> rhs;
  std::optional<Condition<L, A>> condition;
  Searcher custom_search;

  Rewrite(std::string name, Pattern<L> lhs, Pattern<L> rhs,
          std::optional<Condition<L, A>> condition = {})
      : name(std::move(name)), lhs(std::move(lhs)), rhs(std::move(rhs)),
        condition(std::move(condition)) {}
  Rewrite(std::string name, Searcher search)
      : name(std::move(name)), custom_search(std::move(search)) {}
};
template <Language L, class A>
  requires AnalysisFor<A, L>
void validate_rewrite(const Rewrite<L, A> &rule) {
  if (rule.custom_search) {
    if (rule.lhs || rule.rhs || rule.condition)
      throw std::invalid_argument(
          "custom rewrite cannot also contain patterns");
    return;
  }
  if (!rule.lhs || !rule.rhs)
    throw std::invalid_argument("rewrite requires a searcher");
  rule.lhs->validate();
  rule.rhs->validate();
  const auto bound = rule.lhs->variables();
  const auto check_vars = [&](const std::vector<std::string> &names) {
    for (const auto &name : names)
      if (std::find(bound.begin(), bound.end(), name) == bound.end())
        throw std::invalid_argument("unbound rewrite variable: " + name);
  };
  check_vars(rule.rhs->variables());
  if (rule.condition) {
    if (!rule.condition->check)
      throw std::invalid_argument("empty rewrite condition");
    check_vars(rule.condition->required_variables);
  }
}
} // namespace eggc
