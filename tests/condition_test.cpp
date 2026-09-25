#include "eggc/constant_analysis.hpp"
#include "eggc/parser.hpp"
#include "eggc/runner.hpp"
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
using namespace eggc;

bool check(bool value, const char *message) {
  if (value)
    return true;
  std::cerr << "FAILED: " << message << '\n';
  return false;
}

Rewrite div_self() {
  return parse_rewrite("div-self", "(/ ?x ?x)", "1", known_nonzero("?x"));
}

bool accepts_known_nonzero_values() {
  for (const auto *input : {"(/ 7 7)", "(/ -3 -3)", "(/ (+ 2 3) (+ 2 3))"}) {
    EGraph graph(std::make_shared<ConstantAnalysis>());
    const auto root = graph.add_expr(parse_expr(input));
    const auto report = run(graph, {div_self()});
    const auto one = graph.add("1");
    if (!check(report.reason == StopReason::Saturated,
               "known division saturates") ||
        !check(graph.find(root) == graph.find(one),
               "known nonzero division becomes one") ||
        !check(!report.history.empty() &&
                   report.history[0].condition_checks > 0,
               "condition is checked"))
      return false;
  }
  return true;
}

bool rejects_zero_unknown_and_overflow() {
  for (const auto *input :
       {"(/ 0 0)", "(/ x x)",
        "(/ (+ 9223372036854775807 1) (+ 9223372036854775807 1))"}) {
    EGraph graph(std::make_shared<ConstantAnalysis>());
    const auto root = graph.add_expr(parse_expr(input));
    graph.rebuild();
    const auto before = graph.node_count();
    const auto report = run(graph, {div_self()});
    if (!check(report.reason == StopReason::Saturated,
               "rejected condition saturates") ||
        !check(graph.node_count() == before,
               "rejected condition adds no RHS nodes") ||
        !check(!report.history.empty() &&
                   report.history[0].condition_rejections > 0,
               "rejected condition is reported"))
      return false;
    const auto one = graph.add("1");
    if (!check(graph.find(root) != graph.find(one),
               "rejected division is not one"))
      return false;
  }
  return true;
}

bool later_rewrite_enables_condition() {
  EGraph graph(std::make_shared<ConstantAnalysis>());
  const auto root = graph.add_expr(parse_expr("(/ x x)"));
  const auto report =
      run(graph, {parse_rewrite("x-is-seven", "x", "7"), div_self()});
  const auto one = graph.add("1");
  return check(report.reason == StopReason::Saturated,
               "late fact reaches saturation") &&
         check(graph.find(root) == graph.find(one),
               "later constant fact enables condition") &&
         check(report.history.size() >= 2 &&
                   report.history[0].condition_rejections > 0 &&
                   report.history[1].condition_checks > 0,
               "condition is reevaluated after rebuild");
}

bool configuration_errors_are_clear() {
  bool unbound = false;
  try {
    (void)parse_rewrite("bad", "(/ ?x ?x)", "1", known_nonzero("?y"));
  } catch (const std::invalid_argument &) {
    unbound = true;
  }
  bool empty = false;
  try {
    (void)parse_rewrite("bad", "x", "1", Condition{"empty", {}, {}});
  } catch (const std::invalid_argument &) {
    empty = true;
  }
  bool missing_analysis = false;
  try {
    EGraph graph;
    graph.add_expr(parse_expr("(/ x x)"));
    (void)run(graph, {div_self()});
  } catch (const std::logic_error &) {
    missing_analysis = true;
  }
  return check(unbound, "unbound condition variable is rejected") &&
         check(empty, "empty condition is rejected") &&
         check(missing_analysis, "missing analysis is a configuration error");
}

bool rejected_matches_still_count_toward_resource_limits() {
  EGraph graph(std::make_shared<ConstantAnalysis>());
  graph.add_expr(parse_expr("(/ 0 0)"));
  graph.add_expr(parse_expr("(/ x x)"));
  graph.rebuild();
  const auto before = graph.node_count();
  RunOptions options;
  options.match_limit = 1;
  const auto report = run(graph, {div_self()}, options);
  return check(report.reason == StopReason::MatchLimit,
               "rejected matches still consume the global match budget") &&
         check(graph.node_count() == before,
               "limited search applies no rules") &&
         check(report.history.size() == 1 && report.history[0].matches == 1 &&
                   report.history[0].condition_checks == 1 &&
                   report.history[0].condition_rejections == 1,
               "structural and condition counts remain distinct");
}
} // namespace

int main() {
  return accepts_known_nonzero_values() &&
                 rejects_zero_unknown_and_overflow() &&
                 later_rewrite_enables_condition() &&
                 configuration_errors_are_clear() &&
                 rejected_matches_still_count_toward_resource_limits()
             ? 0
             : 1;
}
