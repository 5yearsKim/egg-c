#pragma once

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace eggc {
template <Language L>
CostPolicy<L> ast_size_cost() {
  return [](const L&, const std::vector<std::size_t>& children)
             -> std::optional<std::size_t> {
    std::size_t cost = 1;
    for (const auto child : children) {
      if (cost > std::numeric_limits<std::size_t>::max() - child)
        return std::nullopt;
      cost += child;
    }
    return cost;
  };
}

template <Language L>
CostPolicy<L> ast_depth_cost() {
  return [](const L&, const std::vector<std::size_t>& children)
             -> std::optional<std::size_t> {
    const auto deepest =
        children.empty() ? 0
                         : *std::max_element(children.begin(), children.end());
    if (deepest == std::numeric_limits<std::size_t>::max()) return std::nullopt;
    return deepest + 1;
  };
}

template <Language L, class A, class Cost>
  requires AnalysisFor<A, L>
Extractor<L, A, Cost>::Extractor(const EGraph<L, A>& graph,
                                 CostPolicy<L, Cost> cost)
    : graph_(&graph), revision_(graph.revision()) {
  graph.require_clean();
  if (!cost) {
    if constexpr (std::same_as<Cost, std::size_t>)
      cost = ast_size_cost<L>();
    else
      throw std::invalid_argument(
          "extractor requires a cost policy for this cost type");
  }

  const auto ids = graph.classes();
  const std::size_t slots = graph.node_count() == 0
                                ? 0
                                : static_cast<std::size_t>(*std::max_element(
                                      ids.begin(), ids.end())) +
                                      1;
  choices_.resize(slots);

  std::deque<Id> pending(ids.begin(), ids.end());
  std::vector<unsigned char> queued(slots, 0);
  for (Id id : ids) queued[id] = 1;
  std::vector<Cost> child_costs;
  while (!pending.empty()) {
    const Id id = pending.front();
    pending.pop_front();
    queued[id] = 0;
    bool improved = false;
    for (const auto& node : graph.nodes(id)) {
      child_costs.clear();
      bool finite = true;
      for (Id child_id : node.children()) {
        const Id child = graph.find(child_id);
        if (!choices_[child]) {
          finite = false;
          break;
        }
        child_costs.push_back(choices_[child]->cost);
      }
      if (!finite) continue;
      const auto candidate = cost(node, child_costs);
      if (!candidate) continue;
      if constexpr (std::floating_point<Cost>) {
        if (!std::isfinite(*candidate))
          throw std::invalid_argument("cost must be finite");
      }
      for (const auto& child : child_costs)
        if (!(child < *candidate))
          throw std::invalid_argument(
              "cost policy must return a cost greater than every child");
      if (!choices_[id] || *candidate < choices_[id]->cost) {
        choices_[id] = Choice{*candidate, node};
        improved = true;
      }
    }
    if (improved)
      for (Id parent : graph.parent_classes(id))
        if (!queued[parent]) {
          queued[parent] = 1;
          pending.push_back(parent);
        }
  }
}

template <Language L, class A, class Cost>
  requires AnalysisFor<A, L>
void Extractor<L, A, Cost>::check_graph() const {
  if (!graph_->is_clean() || graph_->revision() != revision_)
    throw std::logic_error(
        "extractor graph changed after extractor construction");
}

template <Language L, class A, class Cost>
  requires AnalysisFor<A, L>
const typename Extractor<L, A, Cost>::Choice& Extractor<L, A, Cost>::choice(
    Id root) const {
  check_graph();
  root = graph_->find(root);
  if (root >= choices_.size() || !choices_[root])
    throw std::runtime_error("no finite expression represented by e-class");
  return *choices_[root];
}

template <Language L, class A, class Cost>
  requires AnalysisFor<A, L>
Cost Extractor<L, A, Cost>::best_cost(Id root) const {
  return choice(root).cost;
}

template <Language L, class A, class Cost>
  requires AnalysisFor<A, L>
std::pair<Cost, RecExpr<L>> Extractor<L, A, Cost>::find_best(Id root) const {
  const auto root_choice = choice(root);
  const auto best_cost_value = root_choice.cost;
  root = graph_->find(root);

  struct Visit {
    Id id;
    bool expanded;
  };
  std::vector<Visit> stack{{root, false}};
  std::unordered_map<Id, Id> output_ids;
  RecExpr<L> expression;
  while (!stack.empty()) {
    const auto visit = stack.back();
    stack.pop_back();
    const Id id = graph_->find(visit.id);
    if (output_ids.count(id)) continue;
    const auto& selected = choice(id);
    if (!visit.expanded) {
      stack.push_back({id, true});
      for (auto it = std::ranges::rbegin(selected.node.children());
           it != std::ranges::rend(selected.node.children()); ++it)
        if (!output_ids.count(graph_->find(*it))) stack.push_back({*it, false});
      continue;
    }
    L node = selected.node;
    for (Id& child : node.children_mut()) {
      const auto found = output_ids.find(graph_->find(child));
      if (found == output_ids.end())
        throw std::logic_error("extractor choice table contains a cycle");
      child = found->second;
    }
    const Id output = expression.add(std::move(node));
    output_ids.emplace(id, output);
  }
  return {best_cost_value, std::move(expression)};
}

}  // namespace eggc
