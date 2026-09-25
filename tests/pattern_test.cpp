#include "eggc/pattern.hpp"
#include <algorithm>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using Bindings = std::vector<std::pair<std::string, eggc::Id>>;
using BindingSet = std::set<Bindings>;

Bindings bindings_for(const eggc::EGraph &graph,
                      const eggc::Substitution &subst) {
  Bindings result;
  result.reserve(subst.size());
  for (const auto &binding : subst)
    result.emplace_back(binding.first, graph.find(binding.second));
  std::sort(result.begin(), result.end());
  return result;
}

BindingSet bindings_for(const eggc::EGraph &graph,
                        const std::vector<eggc::Substitution> &matches) {
  BindingSet result;
  for (const auto &subst : matches)
    result.insert(bindings_for(graph, subst));
  return result;
}

Bindings binding(std::string variable, eggc::Id id) {
  return {{std::move(variable), id}};
}

bool expect(const char *name, const eggc::EGraph &graph,
            const std::vector<eggc::Substitution> &actual,
            BindingSet expected) {
  const auto got = bindings_for(graph, actual);
  if (got == expected && got.size() == actual.size())
    return true;
  std::cerr << "FAILED: " << name << " (expected " << expected.size()
            << " distinct matches, got " << actual.size() << " results / "
            << got.size() << " distinct matches)\n";
  return false;
}

bool multiple_alternatives() {
  eggc::EGraph graph;
  const auto a = graph.add("a");
  const auto b = graph.add("b");
  const auto fa = graph.add("f", {a});
  const auto fb = graph.add("f", {b});
  graph.merge(fa, fb);
  graph.rebuild();

  const auto pattern = eggc::Pattern::node("f", {eggc::Pattern::var("x")});
  return expect("all alternatives in an e-class", graph,
                eggc::match(graph, pattern, fa),
                {binding("?x", a), binding("?x", b)});
}

bool backtracks_for_later_child() {
  eggc::EGraph graph;
  const auto a = graph.add("a");
  const auto b = graph.add("b");
  const auto fa = graph.add("f", {a});
  const auto fb = graph.add("f", {b});
  graph.merge(fa, fb);
  const auto root = graph.add("pair", {fa, b});
  graph.rebuild();

  const auto pattern = eggc::Pattern::node(
      "pair", {eggc::Pattern::node("f", {eggc::Pattern::var("x")}),
               eggc::Pattern::var("x")});
  return expect("backtracking across child patterns", graph,
                eggc::match(graph, pattern, root), {binding("?x", b)});
}

bool independent_alternatives_form_combinations() {
  eggc::EGraph graph;
  const auto a = graph.add("a");
  const auto b = graph.add("b");
  const auto c = graph.add("c");
  const auto d = graph.add("d");
  const auto fa = graph.add("f", {a});
  const auto fb = graph.add("f", {b});
  const auto gc = graph.add("g", {c});
  const auto gd = graph.add("g", {d});
  graph.merge(fa, fb);
  graph.merge(gc, gd);
  const auto root = graph.add("pair", {fa, gc});
  graph.rebuild();

  const auto pattern = eggc::Pattern::node(
      "pair", {eggc::Pattern::node("f", {eggc::Pattern::var("x")}),
               eggc::Pattern::node("g", {eggc::Pattern::var("y")})});
  const BindingSet expected{
      {{"?x", a}, {"?y", c}},
      {{"?x", a}, {"?y", d}},
      {{"?x", b}, {"?y", c}},
      {{"?x", b}, {"?y", d}},
  };
  return expect("cartesian product of child alternatives", graph,
                eggc::match(graph, pattern, root), expected);
}

bool variables_literals_and_repeated_variables() {
  eggc::EGraph graph;
  const auto a = graph.add("a");
  const auto b = graph.add("b");
  const auto aa = graph.add("pair", {a, a});
  const auto ab = graph.add("pair", {a, b});
  graph.rebuild();

  const auto literal = eggc::Pattern::node("a");
  const auto root_var = eggc::Pattern::var("root");
  const auto same = eggc::Pattern::node(
      "pair", {eggc::Pattern::var("x"), eggc::Pattern::var("x")});
  const auto wrong_arity =
      eggc::Pattern::node("pair", {eggc::Pattern::var("x")});
  return expect("literal match has one empty substitution", graph,
                eggc::match(graph, literal, a), {Bindings{}}) &&
         expect("root variable", graph, eggc::match(graph, root_var, a),
                {binding("?root", a)}) &&
         expect("repeated variable matches equal children", graph,
                eggc::match(graph, same, aa), {binding("?x", a)}) &&
         expect("repeated variable rejects unequal children", graph,
                eggc::match(graph, same, ab), {}) &&
         expect("wrong arity", graph, eggc::match(graph, wrong_arity, aa), {});
}

bool merged_ids_are_canonical_and_cycles_terminate() {
  eggc::EGraph graph;
  const auto a = graph.add("a");
  const auto b = graph.add("b");
  graph.merge(a, b);
  graph.rebuild();
  const auto representative = graph.find(a);
  const auto pattern = eggc::Pattern::var("x");
  const auto matches = eggc::match(graph, pattern, b);
  if (!expect("variable match through an old ID", graph, matches,
              {binding("?x", representative)}))
    return false;
  if (matches.size() != 1 || matches[0].at("?x") != representative) {
    std::cerr << "FAILED: variable binding is not canonical\n";
    return false;
  }

  const auto fa = graph.add("f", {a});
  graph.merge(a, fa);
  graph.rebuild();
  const auto nested = eggc::Pattern::node(
      "f", {eggc::Pattern::node("f", {eggc::Pattern::var("x")})});
  return expect("finite pattern over a cyclic e-class", graph,
                eggc::match(graph, nested, a), {binding("?x", graph.find(a))});
}

bool dirty_graph_queries_fail_explicitly() {
  eggc::EGraph graph;
  const auto a = graph.add("a");
  const auto b = graph.add("b");
  graph.rebuild();
  graph.merge(a, b);
  bool nodes_rejected = false;
  bool match_rejected = false;
  try {
    (void)graph.nodes(a);
  } catch (const std::logic_error &) {
    nodes_rejected = true;
  }
  try {
    (void)eggc::match(graph, eggc::Pattern::node("b"), a);
  } catch (const std::logic_error &) {
    match_rejected = true;
  }
  graph.rebuild();
  return nodes_rejected && match_rejected &&
         expect("rebuilt graph exposes merged class members", graph,
                eggc::match(graph, eggc::Pattern::node("b"), a), {Bindings{}});
}

bool operator_names_are_not_inferred_as_variables() {
  eggc::EGraph graph;
  const auto id = graph.add("?literal");
  graph.rebuild();
  const auto pattern = eggc::Pattern::node("?literal");
  return !pattern.is_var() &&
         expect("literal operator with leading question mark", graph,
                eggc::match(graph, pattern, id), {Bindings{}});
}
} // namespace

int main() {
  const bool passed = multiple_alternatives() && backtracks_for_later_child() &&
                      independent_alternatives_form_combinations() &&
                      variables_literals_and_repeated_variables() &&
                      merged_ids_are_canonical_and_cycles_terminate() &&
                      dirty_graph_queries_fail_explicitly() &&
                      operator_names_are_not_inferred_as_variables();
  return passed ? 0 : 1;
}
