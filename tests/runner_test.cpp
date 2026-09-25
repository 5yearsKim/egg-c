#include "eggc/runner.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace eggc;

bool check(bool condition, const char *description) {
  if (condition)
    return true;
  std::cerr << "FAILED: " << description << '\n';
  return false;
}

Pattern v(const char *name) { return Pattern::var(name); }
Pattern n(const char *op, std::vector<Pattern> children = {}) {
  return Pattern::node(op, std::move(children));
}

bool dirty_input_is_rebuilt_before_search() {
  EGraph graph;
  const auto a = graph.add("a");
  const auto b = graph.add("b");
  const auto c = graph.add("c");
  graph.merge(a, b); // Deliberately leave the graph dirty.
  const auto report = run(graph, {{"b-to-c", n("b"), n("c")}});
  return check(report.reason == StopReason::Saturated,
               "dirty input reaches saturation") &&
         check(graph.find(a) == graph.find(c),
               "runner sees the merged class member") &&
         check(report.nodes == graph.node_count(),
               "report reflects rebuilt node count");
}

bool operator_index_tracks_root_operators_and_arity() {
  EGraph graph;
  graph.add("a");
  const auto f1 = graph.add("f", {graph.add("x")});
  const auto f2 = graph.add("f", {graph.add("y")});
  graph.add("f", {graph.add("x"), graph.add("y")});
  graph.rebuild();
  bool dirty_rejected = false;
  graph.add("z");
  try {
    (void)graph.classes_for_op("f", 1);
  } catch (const std::logic_error &) {
    dirty_rejected = true;
  }
  graph.rebuild();
  const auto &unary = graph.classes_for_op("f", 1);
  return check(dirty_rejected, "operator index rejects dirty reads") &&
         check(unary.size() == 2,
               "operator index keeps e-classes, not node count") &&
         check(graph.find(unary[0]) == graph.find(f1) &&
                   graph.find(unary[1]) == graph.find(f2),
               "operator index returns unary f classes") &&
         check(graph.classes_for_op("missing", 0).empty(),
               "missing operator returns no candidates");
}

bool new_matches_wait_until_the_next_iteration() {
  EGraph graph;
  const auto a = graph.add("a");
  const auto input = graph.add("f", {a});
  RunOptions options;
  options.iteration_limit = 1;
  const std::vector<Rewrite> rules{
      {"make-g", n("f", {v("x")}), n("g", {v("x")})},
      {"make-h", n("g", {v("x")}), n("h", {v("x")})},
  };
  const auto report = run(graph, rules, options);
  const auto g = graph.add("g", {a});
  const auto h = graph.add("h", {a});
  graph.rebuild();
  return check(report.reason == StopReason::IterationLimit,
               "one-iteration run hits its limit") &&
         check(graph.find(input) == graph.find(g), "first rule was applied") &&
         check(graph.find(input) != graph.find(h),
               "second rule waits for the next search");
}

bool invalid_rule_is_rejected_before_any_mutation() {
  EGraph graph;
  const auto a = graph.add("a");
  const auto before = graph.revision();
  const std::vector<Rewrite> rules{
      {"valid", n("a"), n("wrap", {n("a")})},
      {"invalid", n("a"), v("missing")},
  };
  try {
    (void)run(graph, rules);
  } catch (const std::invalid_argument &error) {
    return check(std::string(error.what()).find("invalid") != std::string::npos,
                 "validation error identifies the rule") &&
           check(graph.revision() == before,
                 "validation happens before graph mutation") &&
           check(graph.find(a) == a, "original class remains intact");
  }
  return check(false, "invalid RHS variable is rejected");
}

bool saturated_graph_does_not_repeat_for_redundant_rewrites() {
  EGraph graph;
  const auto a = graph.add("a");
  const auto report = run(graph, {{"a-equals-a", n("a"), n("a")}});
  return check(report.reason == StopReason::Saturated,
               "redundant rewrite reaches saturation") &&
         check(report.iterations == 1,
               "saturation is reported on the first complete pass") &&
         check(report.history.size() == 1 && report.history[0].completed,
               "completed iteration is recorded") &&
         check(report.history[0].rewrite_unions == 0,
               "redundant merge is not counted") &&
         check(graph.find(a) == a, "root ID remains valid");
}

bool rebuild_congruence_is_reported_as_progress() {
  EGraph graph;
  const auto a = graph.add("a");
  const auto b = graph.add("b");
  const auto fa = graph.add("f", {a});
  const auto fb = graph.add("f", {b});
  const auto report = run(graph, {{"a-equals-b", n("a"), n("b")}});
  return check(report.reason == StopReason::Saturated,
               "congruence rewrite eventually saturates") &&
         check(graph.find(fa) == graph.find(fb),
               "rebuild merges congruent parent nodes") &&
         check(!report.history.empty() && report.history[0].rewrite_unions == 1,
               "direct rewrite union is counted") &&
         check(!report.history.empty() && report.history[0].rebuild_unions == 1,
               "congruence union is counted as rebuild progress");
}

bool node_limit_returns_a_rebuilt_graph() {
  EGraph graph;
  const auto a = graph.add("a");
  const auto input = graph.add("f", {a});
  RunOptions options;
  options.iteration_limit = 10;
  options.node_limit = 3;
  const auto report =
      run(graph, {{"make-g", n("f", {v("x")}), n("g", {v("x")})}}, options);
  const auto g = graph.add("g", {a});
  graph.rebuild();
  return check(report.reason == StopReason::NodeLimit,
               "node limit is reported") &&
         check(report.nodes == 3,
               "node count includes the newly added RHS node") &&
         check(graph.find(input) == graph.find(g),
               "rewrite is fully applied before stopping") &&
         check(report.history.size() == 1 && !report.history[0].completed,
               "limited iteration is marked incomplete");
}

bool match_limit_does_not_apply_a_partial_search() {
  EGraph graph;
  graph.add("a");
  graph.add("b");
  const auto before = graph.revision();
  RunOptions options;
  options.match_limit = 1;
  const auto report =
      run(graph, {{"wrap-each", v("x"), n("wrap", {v("x")})}}, options);
  return check(report.reason == StopReason::MatchLimit,
               "excess matches stop the run") &&
         check(report.iterations == 1,
               "limited search counts as an attempted iteration") &&
         check(graph.revision() == before,
               "no rewrite applies after interrupted search") &&
         check(report.history.size() == 1 && !report.history[0].completed,
               "interrupted search is marked incomplete");
}

bool exact_match_limit_allows_search_to_complete() {
  EGraph graph;
  graph.add("a");
  graph.add("b");
  RunOptions options;
  options.iteration_limit = 1;
  options.match_limit = 2;
  const auto report =
      run(graph, {{"wrap-each", v("x"), n("wrap", {v("x")})}}, options);
  return check(report.reason == StopReason::IterationLimit,
               "exact match limit is accepted when search completes") &&
         check(report.history.size() == 1 && report.history[0].matches == 2 &&
                   report.history[0].applications == 2,
               "all matches at the limit are applied");
}

bool per_rule_backoff_eventually_searches_every_match() {
  EGraph graph;
  const auto a = graph.add("a");
  const auto b = graph.add("b");
  const auto c = graph.add("c");
  const auto fa = graph.add("f", {a});
  const auto fb = graph.add("f", {b});
  const auto fc = graph.add("f", {c});
  graph.merge(fa, fb);
  graph.merge(fa, fc);
  graph.rebuild();

  RunOptions options;
  options.iteration_limit = 8;
  options.per_rule_match_limit = 1;
  const auto report =
      run(graph, {{"unwrap", n("f", {v("x")}), v("x")}}, options);
  std::size_t deferred = 0;
  for (const auto &iteration : report.history)
    deferred += iteration.backed_off_rules;
  return check(report.reason == StopReason::Saturated,
               "backoff eventually reaches saturation") &&
         check(graph.find(a) == graph.find(b) && graph.find(b) == graph.find(c),
               "matches beyond the first budget are eventually applied") &&
         check(deferred > 0, "runner reports the deferred rule iterations");
}

bool zero_time_limit_stops_before_search() {
  EGraph graph;
  graph.add("a");
  RunOptions options;
  options.time_limit = std::chrono::milliseconds(0);
  const auto report = run(graph, {{"identity", v("x"), v("x")}}, options);
  return check(report.reason == StopReason::TimeLimit,
               "zero time limit reports timeout") &&
         check(report.iterations == 0,
               "timeout before search starts no iteration");
}

bool zero_iteration_limit_rebuilds_input() {
  EGraph graph;
  const auto a = graph.add("a");
  const auto b = graph.add("b");
  graph.merge(a, b);
  RunOptions options;
  options.iteration_limit = 0;
  const auto report = run(graph, {}, options);
  return check(report.reason == StopReason::IterationLimit,
               "zero iterations reports limit") &&
         check(report.iterations == 0, "zero iterations performs no search") &&
         check(graph.node_count() == 2, "input was rebuilt before returning");
}
} // namespace

int main() {
  const bool passed =
      dirty_input_is_rebuilt_before_search() &&
      operator_index_tracks_root_operators_and_arity() &&
      new_matches_wait_until_the_next_iteration() &&
      invalid_rule_is_rejected_before_any_mutation() &&
      saturated_graph_does_not_repeat_for_redundant_rewrites() &&
      rebuild_congruence_is_reported_as_progress() &&
      node_limit_returns_a_rebuilt_graph() &&
      match_limit_does_not_apply_a_partial_search() &&
      exact_match_limit_allows_search_to_complete() &&
      per_rule_backoff_eventually_searches_every_match() &&
      zero_time_limit_stops_before_search() &&
      zero_iteration_limit_rebuilds_input();
  return passed ? 0 : 1;
}
