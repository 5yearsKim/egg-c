#include <numeric>

#include "support/check.hpp"
namespace {
using namespace test_support;
void shared_replacements_and_rules() {
  Pattern rhs = Pattern::var("x");
  for (eggc::Id i = 1; i < 30; ++i)
    rhs.nodes.push_back(Node::node("pair", {i - 1, i - 1}));
  std::vector<eggc::Rewrite<Node>> source{
      {"shared", eggc::parse_pattern("(trigger ?x)"), rhs}};
  eggc::CompiledRules<Node> compiled(source);
  source.clear();
  for (int i = 0; i < 2; ++i) {
    Graph graph;
    auto root = graph.add_expr(eggc::parse_expr("(trigger x)"));
    auto report = eggc::run(graph, compiled);
    check(report.reason == eggc::StopReason::Saturated,
          "shared program did not saturate");
    check(graph.node_count() == 31,
          "replacement unfolded or lost shared entries");
    auto term = graph.lookup(Node::leaf("x"));
    for (int level = 1; level < 30; ++level) {
      check(term.has_value(), "shared RHS lost a child");
      term = graph.lookup(Node::node("pair", {*term, *term}));
    }
    check(term == graph.find(root), "shared RHS was not equivalent to target");
    graph.check_invariants();
  }
}
void resumable_backoff() {
  Graph graph;
  for (int i = 0; i < 7; ++i) graph.add(Node::leaf("a" + std::to_string(i)));
  eggc::CompiledRules<Node> rules(
      {{"wrap", Pattern::var("x"), eggc::parse_pattern("(f ?x)")}});
  eggc::Runner<Node> runner(graph, rules);
  eggc::RunOptions options;
  options.iteration_limit = 1;
  options.per_rule_match_limit = 2;
  options.collect_rule_stats = true;
  runner.resume(options);
  check(runner.report().history.back().rules[0].backed_off,
        "first slice did not back off");
  runner.resume(options);
  check(runner.report().history.back().rules[0].backed_off,
        "second slice lost doubled budget");
  runner.resume(options);
  check(!runner.report().history.back().rules[0].backed_off &&
            runner.report().history.back().applications == 7,
        "resuming reset scheduling state");
  check(runner.report().iterations == 3 &&
            runner.report().history.size() == 3 &&
            runner.report().preparation_rebuilds.size() == 3,
        "resume lost cumulative history");
  options.per_rule_match_limit = 1;
  runner.resume(options);
  check(runner.report().history.back().rules[0].backed_off,
        "changed budget was not applied");
  graph.check_invariants();
}
void stopped_hook_statistics() {
  for (bool stop_hook : {false, true}) {
    Graph graph;
    graph.add(Node::leaf("seed"));
    graph.rebuild();
    const auto before = graph.analysis_revision();
    std::vector<eggc::IterationHook<Node>> hooks{
        [&](Graph& g, const eggc::RunReport&) {
          g.add(Node::leaf("hook"));
          return !stop_hook;
        }};
    eggc::RunOptions options;
    options.match_limit = 0;
    auto report =
        eggc::run(graph, std::vector{eggc::rewrite("seed", "seed", "result")},
                  options, hooks);
    check(report.history.front().analysis_changes ==
              graph.analysis_revision() - before,
          "partial iteration lost analysis changes");
    check(report.history.front().analysis_changes == 1 &&
              !report.history.front().completed,
          "partial iteration statistics are inconsistent");
    check(report.reason == (stop_hook ? eggc::StopReason::UserRequested
                                      : eggc::StopReason::MatchLimit),
          "wrong partial iteration stop reason");
  }
}
void recursive_resume_and_cancelled_cursor() {
  Graph graph;
  auto root = graph.add(Node::leaf("x"));
  eggc::Runner<Node>* active = nullptr;
  std::vector<eggc::IterationHook<Node>> hooks{
      [&](Graph&, const eggc::RunReport&) {
        throws<std::logic_error>([&] { active->resume(); });
        return false;
      }};
  eggc::Runner<Node> runner(graph, eggc::CompiledRules<Node>({}), hooks);
  active = &runner;
  check(runner.resume().reason == eggc::StopReason::UserRequested,
        "recursive resume was not rejected");
  eggc::CompiledPattern<Node> pattern(Pattern::var("x"));
  eggc::MatcherWorkspace workspace;
  pattern.start(graph, root, workspace);
  check(pattern.next(graph, workspace, [] { return true; }) ==
            eggc::MatchStatus::Cancelled,
        "cursor did not cancel");
  check(pattern.next(graph, workspace) == eggc::MatchStatus::Done,
        "cancelled cursor resumed unexpectedly");
  pattern.start(graph, root, workspace);
  check(pattern.next(graph, workspace) == eggc::MatchStatus::Match,
        "cancelled cursor could not restart");
}
void dag_resource_limits() {
  Graph graph;
  auto root = graph.add(Node::leaf("a0"));
  for (int i = 1; i < 1000; ++i)
    graph.merge(root, graph.add(Node::leaf("a" + std::to_string(i))));
  graph.rebuild();
  std::size_t calls = 0;
  eggc::DagExtractor<Node> extractor(graph,
                                     [&](const Node&) -> std::optional<double> {
                                       ++calls;
                                       return 1.;
                                     });
  eggc::DagOptions options;
  options.state_limit = 1;
  auto result = extractor.solve(root, options);
  check(result.explored_states == 1 && calls == 1 &&
            result.peak_frontier <= 2 && !result.optimal,
        "one state generated the entire frontier");
  check(result.reason == eggc::DagStopReason::StateLimit,
        "DAG state limit not reported");
  options.state_limit = 100;
  options.frontier_limit = 1;
  calls = 0;
  result = extractor.solve(root, options);
  check(calls == 1 && result.reason == eggc::DagStopReason::FrontierLimit,
        "frontier limit ignored");
  options.frontier_limit = 100;
  options.time_limit = std::chrono::milliseconds(0);
  calls = 0;
  result = extractor.solve(root, options);
  check(calls == 0 && result.reason == eggc::DagStopReason::TimeLimit,
        "expired DAG budget evaluated costs");
}
void scoped_extraction_and_memory() {
  Graph graph;
  for (int i = 0; i < 2000; ++i)
    graph.add(Node::leaf("unused" + std::to_string(i)));
  auto root = graph.add_expr(eggc::parse_expr("(f (g x))"));
  graph.rebuild();
  std::size_t calls = 0;
  eggc::CostPolicy<Node> cost = [&](const Node& n,
                                    const std::vector<std::size_t>& children)
      -> std::optional<std::size_t> {
    ++calls;
    check(!n.op.starts_with("unused"),
          "scoped extraction visited unrelated nodes");
    return std::accumulate(children.begin(), children.end(), std::size_t{1});
  };
  eggc::Extractor<Node> scoped(graph, std::vector{root}, cost);
  check(
      scoped.best_cost(root) == 3 && scoped.stats().classes == 3 && calls <= 5,
      "scoped extraction scanned entire graph");
  throws<std::out_of_range>(
      [&] { scoped.best_cost(*graph.lookup(Node::leaf("unused0"))); });
  eggc::Extractor<Node> empty(graph, std::vector<eggc::Id>{});
  check(empty.stats().classes == 0, "empty extraction roots scanned graph");
  const auto memory = graph.memory_stats();
  check(
      memory.live_nodes == graph.node_count() &&
          memory.allocated_nodes == memory.live_nodes + memory.retired_nodes &&
          memory.estimated_bytes > sizeof(graph),
      "invalid memory accounting");
  eggc::RunOptions options;
  options.memory_limit_bytes = memory.estimated_bytes;
  auto report = eggc::run(graph, std::vector<eggc::Rewrite<Node>>{}, options);
  check(report.reason == eggc::StopReason::MemoryLimit && graph.is_clean(),
        "memory limit ignored");
}
void retained_storage_and_apply_budget() {
  Graph graph;
  auto a = graph.add(Node::leaf("a")), b = graph.add(Node::leaf("b"));
  graph.add(Node::node("f", {a}));
  graph.add(Node::node("f", {b}));
  graph.merge(a, b);
  graph.rebuild();
  auto retained = graph.memory_stats();
  check(retained.allocated_nodes == 4 && retained.retired_nodes == 1 &&
            retained.live_nodes == 3,
        "retired arena storage was not counted");
  Graph growing;
  growing.add(Node::leaf("x"));
  growing.rebuild();
  eggc::RunOptions options;
  options.memory_limit_bytes = growing.memory_stats().estimated_bytes + 100;
  auto report = eggc::run(
      growing, std::vector{eggc::rewrite("grow", "?x", "(f ?x)")}, options);
  check(report.reason == eggc::StopReason::MemoryLimit && growing.is_clean() &&
            growing.node_count() > 1 && !report.history.back().completed,
        "memory budget did not stop application growth cleanly");
}
void lazy_indexes_and_cursor() {
  Graph graph;
  std::vector<eggc::Id> leaves;
  for (int i = 0; i < 2000; ++i) {
    auto leaf = graph.add(Node::leaf("x" + std::to_string(i)));
    leaves.push_back(leaf);
    graph.add(Node::node("f", {leaf}));
  }
  graph.rebuild();
  graph.classes_for_op("f");
  graph.reset_index_stats();
  graph.merge(leaves[0], leaves[1]);
  graph.rebuild();
  check(graph.index_stats().copied_candidate_ids == 0,
        "rebuild eagerly copied candidates");
  check(graph.classes_for_op("f").size() == 1999 &&
            graph.index_stats().materializations == 1,
        "lazy index was stale");
  const auto copied = graph.index_stats().copied_candidate_ids;
  graph.classes_for_op("f");
  check(graph.index_stats().copied_candidate_ids == copied,
        "clean index copied twice");
  eggc::CompiledPattern<Node> pattern(eggc::parse_pattern("(f ?x)"));
  eggc::MatcherWorkspace workspace;
  auto roots = graph.classes_for_op("f");
  pattern.start(graph, roots.front(), workspace);
  check(pattern.next(graph, workspace) == eggc::MatchStatus::Match,
        "cursor did not yield match");
  check(pattern.next(graph, workspace) == eggc::MatchStatus::Done,
        "cursor repeated match");
  const auto capacity = workspace.registers.capacity();
  for (auto root : roots)
    pattern.search(graph, root, workspace, [](const auto&) { return true; });
  check(workspace.registers.capacity() == capacity, "workspace was not reused");
  pattern.start(graph, roots.front(), workspace);
  graph.add(Node::leaf("later"));
  graph.rebuild();
  throws<std::logic_error>([&] { pattern.next(graph, workspace); });
  graph.check_invariants();
}
void semantic_replay() {
  Graph graph;
  graph.enable_explanations();
  auto root = graph.add_expr(eggc::parse_expr("(+ a 0)"));
  graph.rebuild();
  const auto original_a = graph.lookup(Node::leaf("a"));
  std::vector<eggc::Rewrite<Node>> rules{
      eggc::rewrite("zero", "(+ ?x 0)", "?x")};
  eggc::run(graph, rules);
  check(eggc::verify_rewrites(graph, rules), "named rewrite replay failed");
  std::vector<eggc::Rewrite<Node>> wrong{
      eggc::rewrite("zero", "(+ ?x 0)", "b")};
  check(!eggc::verify_rewrites(graph, wrong),
        "incorrect rewrite accepted by replay");
  auto b = graph.add(Node::leaf("b"));
  auto c = graph.add(Node::leaf("c"));
  auto fb = graph.add(Node::node("f", {b}));
  auto fc = graph.add(Node::node("f", {c}));
  graph.merge(b, c, {eggc::UnionKind::User, "axiom", {}});
  graph.rebuild();
  check(graph.find(fb) == graph.find(fc),
        "assumption did not yield congruence");
  check(!eggc::verify_rewrites(graph, rules), "unaccepted assumption replayed");
  check(eggc::verify_rewrites(graph, rules,
                              [](const eggc::ProofStep& step) {
                                return step.justification.kind ==
                                           eggc::UnionKind::User &&
                                       step.justification.name == "axiom";
                              }),
        "accepted assumption did not replay congruence");
  check(graph.explain_equivalence(root, *original_a).size() == 1,
        "replay altered source graph");
}
void many_clauses_and_proof_replay() {
  Graph graph;
  graph.enable_explanations();
  auto a = graph.add(Node::leaf("a")), b = graph.add(Node::leaf("b"));
  auto fa = graph.add(Node::node("f", {a})),
       fb = graph.add(Node::node("f", {b}));
  graph.merge(a, b, {eggc::UnionKind::User, "assume", {}});
  graph.rebuild();
  check(graph.find(fa) == graph.find(fb), "congruence failed");
  check(graph.verify_explanations([](const eggc::ProofStep& step) {
    return step.justification.kind == eggc::UnionKind::Congruence ||
           step.justification.name == "assume";
  }),
        "independent provenance replay failed");
  check(!graph.verify_explanations([](const auto&) { return false; }),
        "proof checker ignored rejection");
  std::vector<eggc::MultiPattern<Node>::Clause> clauses;
  for (int i = 0; i < 300; ++i)
    clauses.emplace_back("root" + std::to_string(i),
                         eggc::parse_pattern("(f ?x)"));
  auto matches = eggc::MultiPattern<Node>(std::move(clauses)).match(graph);
  check(matches.size() == 1 && matches.front().size() == 301,
        "iterative join lost shared bindings");
}
}  // namespace
int main() {
  return test_support::run_tests(
      shared_replacements_and_rules, resumable_backoff, stopped_hook_statistics,
      dag_resource_limits, scoped_extraction_and_memory,
      lazy_indexes_and_cursor, many_clauses_and_proof_replay, semantic_replay,
      recursive_resume_and_cancelled_cursor, retained_storage_and_apply_budget);
}
