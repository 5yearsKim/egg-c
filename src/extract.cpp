#include "eggc/extract.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace eggc {
CostPolicy ast_size_cost() {
  return [](const ENode &, const std::vector<std::size_t> &children)
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

CostPolicy ast_depth_cost() {
  return [](const ENode &, const std::vector<std::size_t> &children)
             -> std::optional<std::size_t> {
    const auto deepest =
        children.empty() ? 0
                         : *std::max_element(children.begin(), children.end());
    if (deepest == std::numeric_limits<std::size_t>::max())
      return std::nullopt;
    return deepest + 1;
  };
}

Extractor::Extractor(const EGraph &graph, CostPolicy cost)
    : graph_(&graph), revision_(graph.revision()) {
  graph.require_clean();
  if (!cost)
    throw std::invalid_argument("extractor requires a cost policy");

  const auto ids = graph.classes();
  const std::size_t slots =
      graph.node_count() == 0 ? 0
                              : static_cast<std::size_t>(
                                    *std::max_element(ids.begin(), ids.end())) +
                                    1;
  choices_.resize(slots);

  bool changed;
  do {
    changed = false;
    for (const Id id : ids) {
      for (const auto &node : graph.nodes(id)) {
        std::vector<std::size_t> child_costs;
        child_costs.reserve(node.children.size());
        bool finite = true;
        for (const Id child_id : node.children) {
          const auto child = graph.find(child_id);
          if (child >= choices_.size() || !choices_[child]) {
            finite = false;
            break;
          }
          child_costs.push_back(choices_[child]->cost);
        }
        if (!finite)
          continue;

        const auto candidate = cost(node, child_costs);
        if (!candidate)
          continue;
        for (const auto child_cost : child_costs)
          if (*candidate <= child_cost)
            throw std::invalid_argument(
                "cost policy must return a cost greater than every child");

        if (!choices_[id] || *candidate < choices_[id]->cost) {
          choices_[id] = Choice{*candidate, node};
          changed = true;
        }
      }
    }
  } while (changed);
}

void Extractor::check_graph() const {
  if (!graph_->is_clean() || graph_->revision() != revision_)
    throw std::logic_error(
        "extractor graph changed after extractor construction");
}

const Extractor::Choice &Extractor::choice(Id root) const {
  check_graph();
  root = graph_->find(root);
  if (root >= choices_.size() || !choices_[root])
    throw std::runtime_error("no finite expression represented by e-class");
  return *choices_[root];
}

std::size_t Extractor::best_cost(Id root) const { return choice(root).cost; }

Expr Extractor::reconstruct(Id root) const {
  const auto &best = choice(root);
  Expr result{best.node.op, {}};
  result.children.reserve(best.node.children.size());
  for (const Id child : best.node.children)
    result.children.push_back(reconstruct(child));
  return result;
}

ExtractionResult Extractor::find_best(Id root) const {
  const auto &best = choice(root);
  const auto best_cost_value = best.cost;
  return {best_cost_value, reconstruct(root)};
}

std::pair<std::size_t, RecExpr> Extractor::find_best_rec_expr(Id root) const {
  const auto root_choice = choice(root);
  const auto best_cost_value = root_choice.cost;
  root = graph_->find(root);

  struct Visit {
    Id id;
    bool expanded;
  };
  std::vector<Visit> stack{{root, false}};
  std::unordered_map<Id, ExprId> output_ids;
  RecExpr expression;
  while (!stack.empty()) {
    const auto visit = stack.back();
    stack.pop_back();
    const Id id = graph_->find(visit.id);
    if (output_ids.count(id))
      continue;
    const auto &selected = choice(id);
    if (!visit.expanded) {
      stack.push_back({id, true});
      for (auto it = selected.node.children.rbegin();
           it != selected.node.children.rend(); ++it)
        if (!output_ids.count(graph_->find(*it)))
          stack.push_back({*it, false});
      continue;
    }
    ExprNode node{selected.node.op, {}};
    node.children.reserve(selected.node.children.size());
    for (const Id child : selected.node.children) {
      const auto child_id = graph_->find(child);
      const auto found = output_ids.find(child_id);
      if (found == output_ids.end())
        throw std::logic_error("extractor choice table contains a cycle");
      node.children.push_back(found->second);
    }
    const auto output =
        ExprId{static_cast<std::uint32_t>(expression.nodes.size())};
    expression.nodes.push_back(std::move(node));
    output_ids.emplace(id, output);
  }
  return {best_cost_value, std::move(expression)};
}

Expr extract(const EGraph &graph, Id root) {
  return Extractor(graph).find_best(root).expression;
}

} // namespace eggc
