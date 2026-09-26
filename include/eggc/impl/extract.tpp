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
    : Extractor(graph, std::move(cost), std::nullopt) {}

template <Language L, class A, class Cost>
  requires AnalysisFor<A, L>
Extractor<L, A, Cost>::Extractor(const EGraph<L, A>& graph,
                                 std::vector<Id> roots,
                                 CostPolicy<L, Cost> cost)
    : Extractor(graph, std::move(cost),
                std::optional<std::vector<Id>>(std::move(roots))) {}

template <Language L, class A, class Cost>
  requires AnalysisFor<A, L>
Extractor<L, A, Cost>::Extractor(const EGraph<L, A>& graph,
                                 CostPolicy<L, Cost> cost,
                                 std::optional<std::vector<Id>> roots)
    : graph_(&graph),
      revision_(graph.revision()),
      restricted_(roots.has_value()) {
  graph.require_clean();
  if (!cost) {
    if constexpr (std::same_as<Cost, std::size_t>)
      cost = ast_size_cost<L>();
    else
      throw std::invalid_argument(
          "extractor requires a cost policy for this cost type");
  }

  std::vector<Id> ids;
  if (restricted_) {
    const auto add = [&](Id raw) {
      const auto id = graph.find(raw);
      if (selected_slots_.emplace(id, ids.size()).second) ids.push_back(id);
    };
    for (Id root : *roots) add(root);
    for (std::size_t i = 0; i < ids.size(); ++i)
      for (const auto& node : graph.nodes(ids[i]))
        for (Id child : node.children()) add(child);
  } else
    ids = graph.classes();
  stats_.classes = ids.size();
  const auto slots =
      restricted_ ? ids.size()
                  : (ids.empty() ? 0
                                 : static_cast<std::size_t>(*std::max_element(
                                       ids.begin(), ids.end())) +
                                       1);
  choices_.resize(slots);

  std::deque<Id> pending(ids.begin(), ids.end());
  std::vector<unsigned char> queued(slots, 0);
  for (Id id : ids) queued[slot(id)] = 1;
  std::vector<Cost> child_costs;
  while (!pending.empty()) {
    const Id id = pending.front();
    pending.pop_front();
    const auto index = slot(id);
    queued[index] = 0;
    bool improved = false;
    for (const auto& node : graph.nodes(id)) {
      ++stats_.evaluated_nodes;
      child_costs.clear();
      bool finite = true;
      for (Id child_id : node.children()) {
        const auto child = slot(graph.find(child_id));
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
      if (!choices_[index] || *candidate < choices_[index]->cost) {
        choices_[index] = Choice{*candidate, node};
        ++stats_.improvements;
        improved = true;
      }
    }
    if (improved)
      graph.for_each_parent_use(id, [&](Id parent) {
        if (restricted_ && !selected_slots_.contains(parent)) return;
        const auto index = slot(parent);
        if (!queued[index]) {
          queued[index] = 1;
          pending.push_back(parent);
        }
      });
  }
}

template <Language L, class A, class Cost>
  requires AnalysisFor<A, L>
std::size_t Extractor<L, A, Cost>::slot(Id id) const {
  if (!restricted_) return static_cast<std::size_t>(id);
  auto found = selected_slots_.find(id);
  if (found == selected_slots_.end())
    throw std::out_of_range("e-class outside extraction roots");
  return found->second;
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
  const auto index = slot(root);
  if (index >= choices_.size() || !choices_[index])
    throw std::runtime_error("no finite expression represented by e-class");
  return *choices_[index];
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
