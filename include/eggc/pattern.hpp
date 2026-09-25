#pragma once
#include "egraph.hpp"
#include <functional>
#include <unordered_map>

namespace eggc {
struct Pattern {
  enum class Kind { Node, Variable };
  std::string op;
  std::vector<Pattern> children;
  Kind kind = Kind::Node;
  static Pattern var(std::string name);
  static Pattern node(std::string op, std::vector<Pattern> children = {});
  bool is_var() const;
};
using Substitution = std::unordered_map<std::string, Id>;
// Match against a rebuilt graph. Returns every distinct binding of pattern
// variables to canonical e-class IDs; result ordering is unspecified.
std::vector<Substitution> match(const EGraph &graph, const Pattern &pattern,
                                Id eclass);
// Enumerate matches until exhausted, on_match returns false, or should_stop
// returns true. Returns true only if enumeration completed.
bool search_matches(const EGraph &graph, const Pattern &pattern, Id eclass,
                    const std::function<bool(const Substitution &)> &on_match,
                    const std::function<bool()> &should_stop = {});
Id instantiate(EGraph &graph, const Pattern &pattern,
               const Substitution &subst);
} // namespace eggc
