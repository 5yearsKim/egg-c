#pragma once
#include <memory>

#include "rewrite.hpp"

namespace eggc {
// A conjunction of root-variable/pattern clauses sharing one substitution.
// Clauses are evaluated by selectivity (operator candidates first).
template <Language L> class MultiPattern {
public:
  using Clause = std::pair<std::string, Pattern<L>>;
  explicit MultiPattern(std::vector<Clause> clauses)
      : clauses_(std::move(clauses)) {
    if (clauses_.empty())
      throw std::invalid_argument("empty multipattern");
    std::set<std::string> names;
    for (auto &[root, pattern] : clauses_) {
      root = std::get<Var>(Pattern<L>::var(root).nodes.front()).name;
      pattern.validate();
      names.insert(root);
      for (const auto &name : pattern.variables())
        names.insert(name);
    }
    variables_.assign(names.begin(), names.end());
    for (const auto &clause : clauses_)
      compiled_.emplace_back(clause.second, variables_);
  }
  const std::vector<std::string> &variables() const noexcept {
    return variables_;
  }
  template <class A, class Callback>
  bool search(const EGraph<L, A> &graph, Callback &&emit,
              const StopCheck &stop = {}) const {
    graph.require_clean();
    std::vector<std::size_t> order(clauses_.size());
    std::iota(order.begin(), order.end(), 0);
    const auto candidates = [&](std::size_t i) {
      const auto *node = std::get_if<L>(&clauses_[i].second.nodes.back());
      return node ? graph.classes_for_op(node->discriminant())
                  : graph.classes();
    };
    std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) {
      return candidates(a).size() < candidates(b).size();
    });
    std::set<std::vector<Id>> seen;
    std::function<bool(std::size_t, std::vector<Id>)> join;
    join = [&](std::size_t depth, std::vector<Id> bindings) {
      if (stop && stop())
        return false;
      if (depth == order.size()) {
        return !seen.insert(bindings).second ||
               emit(compiled_.front().substitution(bindings));
      }
      const auto clause = order[depth];
      const auto slot = static_cast<std::size_t>(
          std::find(variables_.begin(), variables_.end(),
                    clauses_[clause].first) -
          variables_.begin());
      const auto roots = bindings[slot] == invalid_id
                             ? candidates(clause)
                             : std::vector<Id>{bindings[slot]};
      for (Id root : roots) {
        auto seeded = bindings;
        seeded[slot] = graph.find(root);
        if (!compiled_[clause].search(
                graph, root,
                [&](const std::vector<Id> &next) {
                  return join(depth + 1, next);
                },
                stop, std::move(seeded)))
          return false;
      }
      return true;
    };
    return join(0, std::vector<Id>(variables_.size(), invalid_id));
  }
  template <class A>
  std::vector<Substitution> match(const EGraph<L, A> &graph) const {
    std::vector<Substitution> result;
    search(graph, [&](const Substitution &subst) {
      result.push_back(subst);
      return true;
    });
    return result;
  }

private:
  std::vector<Clause> clauses_;
  std::vector<std::string> variables_;
  std::vector<CompiledPattern<L>> compiled_;
};
// Construct a rewrite that joins several patterns and replaces one bound root.
// Custom applications remain available for rewriting several roots at once.
template <Language L, class A = NoAnalysis<L>>
Rewrite<L, A> multi_rewrite(std::string name, MultiPattern<L> lhs,
                            std::string target, Pattern<L> rhs) {
  target = std::get<Var>(Pattern<L>::var(target).nodes.front()).name;
  if (std::find(lhs.variables().begin(), lhs.variables().end(), target) ==
      lhs.variables().end())
    throw std::invalid_argument("unbound multipattern target");
  auto replacement =
      std::make_shared<CompiledPattern<L>>(std::move(rhs), lhs.variables());
  const auto names = lhs.variables();
  return Rewrite<L, A>{
      std::move(name),
      [lhs = std::move(lhs), target = std::move(target), replacement,
       names](const EGraph<L, A> &graph,
              const typename Rewrite<L, A>::Sink &emit, const StopCheck &stop) {
        return lhs.search(
            graph,
            [&](const Substitution &subst) {
              std::vector<Id> bindings;
              for (const auto &name : names)
                bindings.push_back(subst.at(name));
              return emit({subst.at(target),
                           [replacement, bindings = std::move(bindings)](
                               EGraph<L, A> &g) -> std::optional<Id> {
                             return replacement->instantiate(g, bindings);
                           }});
            },
            stop);
      }};
}
} // namespace eggc
