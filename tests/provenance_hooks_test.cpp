
#include "support/check.hpp"

namespace {
using namespace test_support;
void lookup_hooks_and_provenance() {
  Graph graph;
  graph.enable_explanations();
  auto root = graph.add_expr(eggc::parse_expr("(+ a 0)"));
  throws<std::logic_error>([&] { graph.lookup(Node::leaf("a")); });
  graph.rebuild();
  auto before = graph.revision();
  check(graph.lookup_expr(eggc::parse_expr("(+ a 0)")) == graph.find(root),
        "expression lookup failed");
  check(!graph.lookup_expr(eggc::parse_expr("(+ b 0)")),
        "lookup inserted missing expression");
  check(graph.revision() == before, "lookup mutated graph");
  std::vector<eggc::Rewrite<Node>> rules{
      eggc::rewrite("zero", "(+ ?x 0)", "?x")};
  auto a = graph.lookup(Node::leaf("a"));
  eggc::run(graph, rules);
  auto proof = graph.explain_equivalence(root, *a);
  check(!proof.empty() &&
            proof.front().justification.kind == eggc::UnionKind::Rewrite &&
            proof.front().justification.name == "zero",
        "rewrite provenance was lost");
  auto fa = graph.add(Node::node("f", {*a}));
  auto b = graph.add(Node::leaf("b"));
  auto fb = graph.add(Node::node("f", {b}));
  graph.merge(*a, b, {eggc::UnionKind::User, "assumption", {}});
  graph.rebuild();
  auto congruence = graph.explain_equivalence(fa, fb);
  check(congruence.size() == 1 &&
            congruence[0].justification.kind == eggc::UnionKind::Congruence &&
            congruence[0].justification.premises.size() == 1,
        "congruence provenance is missing");
  auto reverse = graph.explain_equivalence(fb, fa);
  check(reverse.front().lhs == fb && reverse.back().rhs == fa,
        "reverse explanation has wrong direction");
  auto foreign = graph.add(Node::leaf("foreign"));
  graph.rebuild();
  throws<std::invalid_argument>(
      [&] { graph.explain_equivalence(root, foreign); });
  graph.check_invariants();

  Graph hooked;
  auto x = hooked.add(Node::leaf("x"));
  std::vector<eggc::IterationHook<Node>> hooks;
  hooks.emplace_back([&](Graph& g, const eggc::RunReport& report) {
    check(g.is_clean(), "iteration hook saw a dirty graph");
    if (report.iterations == 1) {
      g.add(Node::leaf("extra"));
      return true;
    }
    return false;
  });
  auto report = eggc::run(hooked, std::vector<eggc::Rewrite<Node>>{},
                          eggc::RunOptions{}, hooks);
  check(report.reason == eggc::StopReason::UserRequested && hooked.is_clean() &&
            hooked.node_count() == 2,
        "hook mutation caused false saturation or stop left dirty graph");
  check(hooked.lookup(Node::leaf("x")) == x, "hook changed existing root");

  // Mutating the caller's rule/hook vectors cannot invalidate the run snapshot.
  Graph frozen;
  frozen.add(Node::leaf("a"));
  std::vector<eggc::Rewrite<Node>> mutable_rules{
      eggc::rewrite("a-b", "a", "b")};
  std::vector<eggc::IterationHook<Node>> mutable_hooks;
  mutable_hooks.emplace_back([&](Graph&, const eggc::RunReport&) {
    mutable_rules.clear();
    mutable_hooks.clear();
    return true;
  });
  auto snapshot =
      eggc::run(frozen, mutable_rules, eggc::RunOptions{}, mutable_hooks);
  check(snapshot.reason == eggc::StopReason::Saturated &&
            frozen.lookup(Node::leaf("b")),
        "run did not freeze mutable rules and hooks");
}

}  // namespace
int main() { return test_support::run_tests(lookup_hooks_and_provenance); }
