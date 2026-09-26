#pragma once
#include <chrono>
#include <cmath>
#include <functional>
#include <unordered_map>

#include "egraph.hpp"

namespace eggc {
struct DagOptions {
  std::size_t state_limit = 100000;
  std::optional<std::chrono::milliseconds> time_limit;
};
template <Language L> struct DagResult {
  std::optional<double> cost;
  RecExpr<L> expression;
  std::size_t explored_states = 0;
  bool optimal = false;
};
// Counts each selected e-class once. nullopt excludes a node. Costs must be
// deterministic, finite, and nonnegative. Exact search is exponential; limits
// return the best incumbent explicitly marked non-optimal, if one was found.
template <Language L, class A = NoAnalysis<L>> class DagExtractor {
public:
  using Cost = std::function<std::optional<double>(const L &)>;
  explicit DagExtractor(
      const EGraph<L, A> &graph, Cost cost = [](const L &) { return 1.0; })
      : graph_(&graph), revision_(graph.revision()), cost_(std::move(cost)) {
    graph.require_clean();
    if (!cost_)
      throw std::invalid_argument("DAG extractor requires a cost policy");
  }
  DagResult<L> solve(Id root, const DagOptions &options = {}) const {
    if (!graph_->is_clean() || graph_->revision() != revision_)
      throw std::logic_error("extractor graph changed after construction");
    root = graph_->find(root);
    using Selection = std::unordered_map<Id, std::size_t>;
    struct State {
      Selection selection;
      std::vector<Id> pending;
      double cost = 0;
    };
    std::vector<State> stack{{{}, {root}, 0}};
    DagResult<L> result;
    Selection best;
    const auto start = std::chrono::steady_clock::now();
    while (!stack.empty()) {
      if (result.explored_states >= options.state_limit ||
          (options.time_limit &&
           std::chrono::steady_clock::now() - start >= *options.time_limit))
        break;
      State state = std::move(stack.back());
      stack.pop_back();
      ++result.explored_states;
      if (result.cost && state.cost >= *result.cost)
        continue;
      while (!state.pending.empty() &&
             state.selection.contains(state.pending.back()))
        state.pending.pop_back();
      if (state.pending.empty()) {
        result.cost = state.cost;
        best = std::move(state.selection);
        continue;
      }
      const Id id = state.pending.back();
      state.pending.pop_back();
      const auto &nodes = graph_->nodes(id);
      // Reverse insertion explores the class's first node first.
      for (std::size_t i = nodes.size(); i-- > 0;) {
        auto weight = cost_(nodes[i]);
        if (!weight)
          continue;
        if (!std::isfinite(*weight) || *weight < 0)
          throw std::invalid_argument(
              "DAG cost must be finite and nonnegative");
        if (!std::isfinite(state.cost + *weight))
          continue;
        State next = state;
        next.cost += *weight;
        if (result.cost && next.cost >= *result.cost)
          continue;
        next.selection.emplace(id, i);
        if (cyclic(next.selection))
          continue;
        for (Id child : nodes[i].children())
          next.pending.push_back(graph_->find(child));
        stack.push_back(std::move(next));
      }
    }
    result.optimal = stack.empty();
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
      if (output.contains(id))
        continue;
      L node = graph_->nodes(id)[best.at(id)];
      if (!expanded) {
        visits.push_back({id, true});
        for (Id child : node.children())
          if (!output.contains(graph_->find(child)))
            visits.push_back({graph_->find(child), false});
      } else {
        for (Id &child : node.children_mut())
          child = output.at(graph_->find(child));
        output.emplace(id, result.expression.add(std::move(node)));
      }
    }
    return result;
  }

private:
  bool cyclic(const std::unordered_map<Id, std::size_t> &selection) const {
    std::unordered_map<Id, unsigned char> colors;
    for (const auto &[root, unused] : selection) {
      (void)unused;
      if (colors[root] == 2)
        continue;
      std::vector<std::pair<Id, bool>> visits{{root, false}};
      while (!visits.empty()) {
        auto [id, expanded] = visits.back();
        visits.pop_back();
        if (!selection.contains(id))
          continue;
        if (expanded) {
          colors[id] = 2;
          continue;
        }
        if (colors[id] == 1)
          return true;
        if (colors[id] == 2)
          continue;
        colors[id] = 1;
        visits.push_back({id, true});
        for (Id child : graph_->nodes(id)[selection.at(id)].children())
          visits.push_back({graph_->find(child), false});
      }
    }
    return false;
  }
  const EGraph<L, A> *graph_;
  std::uint64_t revision_;
  Cost cost_;
};
} // namespace eggc
