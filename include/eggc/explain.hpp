#pragma once
#include <string>
#include <utility>
#include <vector>

#include "id.hpp"
namespace eggc {
enum class UnionKind { User, Rewrite, Congruence, Analysis };
struct Justification {
  UnionKind kind = UnionKind::User;
  std::string name = "user";
  std::vector<std::pair<Id, Id>> premises;
};
struct ProofStep {
  Id lhs;
  Id rhs;
  Justification justification;
};
}  // namespace eggc
