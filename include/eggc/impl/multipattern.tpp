#pragma once
namespace eggc {
template <Language L>
MultiPattern<L>::MultiPattern(std::vector<Clause> clauses)
    : clauses_(std::move(clauses)) {
  if (clauses_.empty()) throw std::invalid_argument("empty multipattern");
  std::set<std::string> names;
  for (auto& [root, pattern] : clauses_) {
    root = std::get<Var>(Pattern<L>::var(root).nodes.front()).name;
    pattern.validate();
    names.insert(root);
    for (const auto& name : pattern.variables()) names.insert(name);
  }
  variables_.assign(names.begin(), names.end());
  for (const auto& clause : clauses_) {
    compiled_.emplace_back(clause.second, variables_);
    root_slots_.push_back(static_cast<std::size_t>(
        std::find(variables_.begin(), variables_.end(), clause.first) -
        variables_.begin()));
  }
}
template <Language L>
template <class A, class Callback>
bool MultiPattern<L>::search(const EGraph<L, A>& graph, Callback&& emit,
                             const StopCheck& stop) const {
  graph.require_clean();
  std::vector<Id> all_classes;
  std::vector<const std::vector<Id>*> candidates;
  for (const auto& clause : clauses_) {
    const auto* node = std::get_if<L>(&clause.second.nodes.back());
    if (node)
      candidates.push_back(&graph.classes_for_op(node->discriminant()));
    else {
      if (all_classes.empty()) all_classes = graph.classes();
      candidates.push_back(&all_classes);
    }
  }
  std::vector<std::size_t> order(clauses_.size());
  std::iota(order.begin(), order.end(), 0);
  std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) {
    return candidates[a]->size() < candidates[b]->size();
  });
  struct Frame {
    MatcherWorkspace matcher;
    std::vector<Id> seed;
    std::size_t next_root = 0;
    bool active = false;
  };
  std::vector<Frame> frames(clauses_.size());
  frames.front().seed.assign(variables_.size(), invalid_id);
  detail::BindingSet seen;
  seen.reset(variables_.size());
  std::size_t depth = 0;
  for (;;) {
    if (stop && stop()) return false;
    auto& frame = frames[depth];
    const auto clause = order[depth];
    const auto slot = root_slots_[clause];
    if (!frame.active) {
      const bool bound = frame.seed[slot] != invalid_id;
      const auto count = bound ? 1 : candidates[clause]->size();
      if (frame.next_root == count) {
        if (depth == 0) return true;
        --depth;
        continue;
      }
      const Id root =
          bound ? frame.seed[slot] : (*candidates[clause])[frame.next_root];
      ++frame.next_root;
      auto seed = frame.seed;
      seed[slot] = root;
      compiled_[clause].start(graph, root, frame.matcher, std::move(seed));
      frame.active = true;
    }
    const auto status = compiled_[clause].next(graph, frame.matcher, stop);
    if (status == MatchStatus::Cancelled) return false;
    if (status == MatchStatus::Done) {
      frame.active = false;
      continue;
    }
    if (depth + 1 == frames.size()) {
      if (seen.insert(frame.matcher.bindings) &&
          !emit(compiled_.front().substitution(frame.matcher.bindings)))
        return false;
    } else {
      auto& child = frames[++depth];
      child.seed.assign(frame.matcher.bindings.begin(),
                        frame.matcher.bindings.end());
      child.next_root = 0;
      child.active = false;
    }
  }
}
template <Language L>
template <class A>
std::vector<Substitution> MultiPattern<L>::match(
    const EGraph<L, A>& graph) const {
  std::vector<Substitution> result;
  search(graph, [&](const Substitution& subst) {
    result.push_back(subst);
    return true;
  });
  return result;
}
template <Language L, class A>
Rewrite<L, A> multi_rewrite(std::string name, MultiPattern<L> lhs,
                            std::string target, Pattern<L> rhs) {
  target = std::get<Var>(Pattern<L>::var(target).nodes.front()).name;
  if (std::find(lhs.variables().begin(), lhs.variables().end(), target) ==
      lhs.variables().end())
    throw std::invalid_argument("unbound multipattern target");
  auto replacement =
      std::make_shared<CompiledReplacement<L>>(std::move(rhs), lhs.variables());
  const auto names = lhs.variables();
  return Rewrite<L, A>{
      std::move(name),
      [lhs = std::move(lhs), target = std::move(target), replacement, names](
          const EGraph<L, A>& graph, const typename Rewrite<L, A>::Sink& emit,
          const StopCheck& stop) {
        return lhs.search(
            graph,
            [&](const Substitution& subst) {
              std::vector<Id> bindings;
              for (const auto& name : names) bindings.push_back(subst.at(name));
              return emit({subst.at(target),
                           [replacement, bindings = std::move(bindings)](
                               EGraph<L, A>& g) -> std::optional<Id> {
                             return replacement->instantiate(g, bindings);
                           }});
            },
            stop);
      }};
}

}  // namespace eggc
