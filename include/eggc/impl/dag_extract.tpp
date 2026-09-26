#pragma once
namespace eggc {
template <Language L, class A>
DagExtractor<L, A>::DagExtractor(const EGraph<L, A>& graph, Cost cost)
    : graph_(&graph), revision_(graph.revision()), cost_(std::move(cost)) {
  graph.require_clean();
  if (!cost_)
    throw std::invalid_argument("DAG extractor requires a cost policy");
}
template <Language L, class A>
DagResult<L> DagExtractor<L, A>::solve(Id root,
                                       const DagOptions& options) const {
  if (!graph_->is_clean() || graph_->revision() != revision_)
    throw std::logic_error("extractor graph changed after construction");
  root = graph_->find(root);
  using Selection = std::unordered_map<Id, std::size_t>;
  struct State {
    Selection selection;
    std::vector<Id> pending;
    double cost = 0;
  };
  struct Frame {
    State state;
    Id id = invalid_id;
    std::size_t next = 0;
    bool entered = false;
  };
  std::vector<Frame> stack;
  DagResult<L> result;
  Selection best;
  const auto start = std::chrono::steady_clock::now();
  const auto timed_out = [&] {
    return options.time_limit &&
           std::chrono::steady_clock::now() - start >= *options.time_limit;
  };
  if (options.frontier_limit == 0) {
    result.reason = DagStopReason::FrontierLimit;
    return result;
  }
  stack.push_back({State{{}, {root}, 0}});
  result.peak_frontier = 1;
  while (!stack.empty()) {
    if (timed_out()) {
      result.reason = DagStopReason::TimeLimit;
      break;
    }
    auto& frame = stack.back();
    if (!frame.entered) {
      if (result.explored_states >= options.state_limit) {
        result.reason = DagStopReason::StateLimit;
        break;
      }
      ++result.explored_states;
      frame.entered = true;
      auto& state = frame.state;
      if (result.cost && state.cost >= *result.cost) {
        stack.pop_back();
        continue;
      }
      while (!state.pending.empty() &&
             state.selection.contains(state.pending.back()))
        state.pending.pop_back();
      if (state.pending.empty()) {
        result.cost = state.cost;
        best = state.selection;
        stack.pop_back();
        continue;
      }
      frame.id = state.pending.back();
      state.pending.pop_back();
    }
    const auto& nodes = graph_->nodes(frame.id);
    if (frame.next == nodes.size()) {
      stack.pop_back();
      continue;
    }
    // Evaluate one alternative at a time. Siblings stay as an index, not copied
    // states.
    const auto index = frame.next++;
    auto weight = cost_(nodes[index]);
    if (timed_out()) {
      result.reason = DagStopReason::TimeLimit;
      break;
    }
    if (!weight) continue;
    if (!std::isfinite(*weight) || *weight < 0)
      throw std::invalid_argument("DAG cost must be finite and nonnegative");
    if (!std::isfinite(frame.state.cost + *weight)) continue;
    State next = frame.state;
    next.cost += *weight;
    if (result.cost && next.cost >= *result.cost) continue;
    next.selection.emplace(frame.id, index);
    if (cyclic(next.selection, timed_out)) continue;
    for (Id child : nodes[index].children())
      next.pending.push_back(graph_->find(child));
    if (stack.size() >= options.frontier_limit) {
      result.reason = DagStopReason::FrontierLimit;
      break;
    }
    stack.push_back({std::move(next)});
    result.peak_frontier = std::max(result.peak_frontier, stack.size());
  }
  result.optimal = stack.empty() && result.reason == DagStopReason::Exhausted;
  if (!result.cost) {
    if (result.optimal)
      throw std::runtime_error("no finite DAG represented by e-class");
    return result;
  }
  std::unordered_map<Id, Id> output;
  std::vector<std::pair<Id, bool>> visits{{root, false}};
  while (!visits.empty()) {
    const auto [id, expanded] = visits.back();
    visits.pop_back();
    if (output.contains(id)) continue;
    L node = graph_->nodes(id)[best.at(id)];
    if (!expanded) {
      visits.push_back({id, true});
      for (Id child : node.children())
        if (!output.contains(graph_->find(child)))
          visits.push_back({graph_->find(child), false});
    } else {
      for (Id& child : node.children_mut())
        child = output.at(graph_->find(child));
      output.emplace(id, result.expression.add(std::move(node)));
    }
  }
  return result;
}
template <Language L, class A>
template <class Stop>
bool DagExtractor<L, A>::cyclic(
    const std::unordered_map<Id, std::size_t>& selection,
    const Stop& stop) const {
  std::unordered_map<Id, unsigned char> colors;
  for (const auto& [root, unused] : selection) {
    (void)unused;
    if (colors[root] == 2) continue;
    std::vector<std::pair<Id, bool>> visits{{root, false}};
    while (!visits.empty()) {
      if (stop()) return true;
      auto [id, expanded] = visits.back();
      visits.pop_back();
      if (!selection.contains(id)) continue;
      if (expanded) {
        colors[id] = 2;
        continue;
      }
      if (colors[id] == 1) return true;
      if (colors[id] == 2) continue;
      colors[id] = 1;
      visits.push_back({id, true});
      for (Id child : graph_->nodes(id)[selection.at(id)].children())
        visits.push_back({graph_->find(child), false});
    }
  }
  return false;
}
}  // namespace eggc
