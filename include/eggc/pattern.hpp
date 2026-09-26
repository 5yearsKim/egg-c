#pragma once
#include <functional>
#include <string>
#include <unordered_map>
#include <variant>

#include "egraph.hpp"

namespace eggc {
struct Var {
  std::string name;
};
using Substitution = std::unordered_map<std::string, Id>;
using StopCheck = std::function<bool()>;

template <Language L> struct Pattern {
  using Entry = std::variant<Var, L>;
  // Node children index earlier pattern entries, just as in RecExpr. Every
  // entry must be reachable from the final root; DAG sharing is permitted.
  std::vector<Entry> nodes;
  static Pattern var(std::string name);
  // prototype has the intended arity; placeholder children are replaced.
  static Pattern node(L prototype, std::vector<Pattern> children = {});
  void validate() const;
  std::vector<std::string> variables() const;
};

template <Language L, class A, class Callback>
  requires AnalysisFor<A, L>
bool search_matches(const EGraph<L, A> &graph, const Pattern<L> &pattern,
                    Id eclass, Callback &&on_match,
                    const StopCheck &should_stop = {});
template <Language L, class A>
  requires AnalysisFor<A, L>
std::vector<Substitution> match(const EGraph<L, A> &graph,
                                const Pattern<L> &pattern, Id eclass);
template <Language L, class A>
  requires AnalysisFor<A, L>
Id instantiate(EGraph<L, A> &graph, const Pattern<L> &pattern,
               const Substitution &substitution);
} // namespace eggc

#include "impl/pattern.tpp"
