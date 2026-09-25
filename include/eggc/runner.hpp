#pragma once
#include "rewrite.hpp"
#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace eggc {
enum class StopReason { Saturated, IterationLimit, NodeLimit, TimeLimit, MatchLimit };
struct RunOptions {
    std::size_t iteration_limit = 10;
    std::size_t node_limit = 10000;
    std::optional<std::chrono::milliseconds> time_limit;
    // Maximum number of distinct matches collected across all rules per iteration.
    // If exceeded, that iteration's pending matches are discarded.
    std::optional<std::size_t> match_limit;
    // Optional per-rule exponential backoff: defer rules exceeding this initial
    // per-iteration match budget, then double their budget on the next iteration.
    std::optional<std::size_t> per_rule_match_limit;
};
struct IterationStats {
    std::size_t matches = 0;
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
    bool completed = false;
};
struct RunReport {
    StopReason reason = StopReason::Saturated;
    // Number of iterations started; a limited partial iteration is included.
    std::size_t iterations = 0;
    std::size_t nodes = 0;
    std::vector<IterationStats> history;
};
// Validates all rules, rebuilds the input, and searches only clean graph states.
// Time checks are cooperative and may be exceeded by one rewrite application or rebuild.
RunReport run(EGraph& graph, const std::vector<Rewrite>& rules, const RunOptions& options);
RunReport run(EGraph& graph, const std::vector<Rewrite>& rules,
              std::size_t iteration_limit = 10, std::size_t node_limit = 10000);
}
