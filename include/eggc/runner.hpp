#pragma once
#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include "rewrite.hpp"

namespace eggc {
enum class StopReason {
  Saturated,
  IterationLimit,
  NodeLimit,
  TimeLimit,
  MatchLimit,
  // A custom search exhausted its own bounded search budget.
  SearchLimit,
  UserRequested
};
struct RunOptions {
  std::size_t iteration_limit = 10;
  std::size_t node_limit = 10000;
  std::optional<std::chrono::milliseconds> time_limit;
  // Maximum number of distinct structural matches across all rules per
  // iteration, including matches rejected by conditions. If exceeded, that
  // iteration's pending applications are discarded.
  std::optional<std::size_t> match_limit;
  // Optional per-rule exponential backoff: defer rules exceeding this initial
  // per-iteration match budget, then double their budget on the next iteration.
  // All queued applications of a deferred rule are discarded. Its inspected
  // matches still consume the global budget, including rejected conditions.
  std::optional<std::size_t> per_rule_match_limit;
  // Populate IterationStats::rules. Disabled by default to avoid per-rule
  // allocations and clock readings. Duplicate names remain distinct by index.
  bool collect_rule_stats = false;
};
struct RuleStats {
  std::size_t rule_index = 0;
  std::string name;
  std::size_t matches = 0;
  std::size_t condition_checks = 0;
  std::size_t condition_rejections = 0;
  std::size_t applications = 0;
  std::size_t rewrite_unions = 0;
  std::chrono::nanoseconds search_time{0};
  std::chrono::nanoseconds apply_time{0};
  bool searched = false;
  bool search_completed = false;
  bool backed_off = false;
};
struct IterationStats {
  // Number of distinct structural matches, including rejected conditions.
  std::size_t matches = 0;
  std::size_t condition_checks = 0;
  std::size_t condition_rejections = 0;
  std::size_t applications = 0;
  std::size_t rewrite_unions = 0;
  std::size_t rebuild_unions = 0;
  std::size_t analysis_changes = 0;
  std::size_t backed_off_rules = 0;
  std::size_t nodes = 0;
  std::size_t classes = 0;
  std::chrono::nanoseconds search_time{0};
  std::chrono::nanoseconds apply_time{0};
  std::chrono::nanoseconds rebuild_time{0};
  std::chrono::nanoseconds hook_time{0};
  bool completed = false;
  std::size_t analysis_evaluations = 0;
  // Empty unless RunOptions::collect_rule_stats is enabled. Includes unsearched
  // rules when a limit stops the iteration; their searched flag is false.
  std::vector<RuleStats> rules;
};
struct RunReport {
  StopReason reason = StopReason::Saturated;
  // Number of iterations started; a limited partial iteration is included.
  std::size_t iterations = 0;
  std::size_t nodes = 0;
  std::vector<IterationStats> history;
  RebuildStats initial_rebuild;
};
template <Language L, class A = NoAnalysis<L>>
using IterationHook = std::function<bool(EGraph<L, A> &, const RunReport &)>;
// Hooks run before each iteration on a clean graph. Return false to stop.
// The runner rebuilds after each hook, including a hook that requests stop.
// Validates all rules, rebuilds the input, and searches only clean graph
// states. Time checks are cooperative and may be exceeded by one rewrite
// application or rebuild. A custom search returning false before the
// runner's limits are hit reports SearchLimit and discards pending
// applications.
template <Language L, class A>
  requires AnalysisFor<A, L>
RunReport run(EGraph<L, A> &graph, const std::vector<Rewrite<L, A>> &rules,
              const RunOptions &options,
              const std::vector<IterationHook<L, A>> &hooks = {});
template <Language L, class A>
  requires AnalysisFor<A, L>
RunReport run(EGraph<L, A> &graph, const std::vector<Rewrite<L, A>> &rules,
              std::size_t iteration_limit = 10, std::size_t node_limit = 10000);
} // namespace eggc
#include "impl/runner.tpp"
