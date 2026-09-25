#pragma once
#include "pattern.hpp"
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace eggc {
// Conditions inspect only the clean graph used for matching. A condition that
// becomes true must stay true as sound equalities and analysis facts are added.
struct Condition {
  std::string name;
  std::vector<std::string> required_variables;
  std::function<bool(const EGraph &, Id, const Substitution &)> check;
};
struct Rewrite {
  std::string name;
  Pattern lhs;
  Pattern rhs;
  std::optional<Condition> condition = std::nullopt;
};
void validate_rewrite(const Rewrite &rewrite);
} // namespace eggc
