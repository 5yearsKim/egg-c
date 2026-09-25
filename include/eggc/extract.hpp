#pragma once
#include "egraph.hpp"
#include "expr.hpp"
#include <functional>
#include <optional>

namespace eggc {
// Returning nullopt means the node cost is unrepresentable or inapplicable.
// A valid policy must be deterministic, nondecreasing, and return a cost
// strictly greater than each child cost.
using CostPolicy = std::function<std::optional<std::size_t>(
    const ENode &, const std::vector<std::size_t> &)>;

CostPolicy ast_size_cost();
CostPolicy ast_depth_cost();

struct ExtractionResult {
  std::size_t cost;
  Expr expression;
};

class Extractor {
public:
  explicit Extractor(const EGraph &graph, CostPolicy cost = ast_size_cost());
  std::size_t best_cost(Id root) const;
  ExtractionResult find_best(Id root) const;
  std::pair<std::size_t, RecExpr> find_best_rec_expr(Id root) const;

private:
  struct Choice {
    std::size_t cost;
    ENode node;
  };
  const EGraph *graph_;
  std::uint64_t revision_;
  std::vector<std::optional<Choice>> choices_;
  void check_graph() const;
  const Choice &choice(Id root) const;
  Expr reconstruct(Id root) const;
};

Expr extract(const EGraph &graph, Id root);
} // namespace eggc
