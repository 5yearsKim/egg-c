#pragma once
#include <stdexcept>
#include <utility>
#include <vector>

#include "language.hpp"

namespace eggc {
// A bottom-up DAG of application nodes. Here child IDs index earlier entries;
// in an EGraph the same node type instead refers to e-classes.
template <Language L>
struct RecExpr {
  std::vector<L> nodes;
  Id root() const {
    if (nodes.empty()) throw std::logic_error("empty expression has no root");
    return static_cast<Id>(nodes.size() - 1);
  }
  Id add(L node) {
    if (nodes.size() >= invalid_id)
      throw std::overflow_error("expression too large");
    for (Id child : node.children())
      if (child >= nodes.size())
        throw std::invalid_argument("expression children must precede parent");
    Id id = static_cast<Id>(nodes.size());
    nodes.push_back(std::move(node));
    return id;
  }
};
}  // namespace eggc
