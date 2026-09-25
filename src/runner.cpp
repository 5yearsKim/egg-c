#include "eggc/runner.hpp"
#include <chrono>
#include <functional>
#include <limits>
#include <stdexcept>
#include <utility>

namespace eggc {
namespace {
using Clock = std::chrono::steady_clock;

void validate_rules(const std::vector<Rewrite> &rules) {
  for (const auto &rule : rules)
    validate_rewrite(rule);
}

struct PendingMatch {
  std::size_t rule_index;
  Id target;
  Substitution substitution;
};

struct SearchResult {
  std::vector<PendingMatch> pending;
  std::size_t matches = 0;
  std::size_t condition_checks = 0;
  std::size_t condition_rejections = 0;
  std::size_t backed_off_rules = 0;
  bool any_backoff = false;
  std::optional<StopReason> stop;
};

SearchResult search_iteration(const EGraph &graph,
                              const std::vector<Rewrite> &rules,
                              const RunOptions &options,
                              std::vector<std::size_t> &rule_budgets,
                              const std::function<bool()> &timed_out) {
  SearchResult result;
  for (std::size_t rule_index = 0; rule_index < rules.size(); ++rule_index) {
    std::size_t rule_matches = 0;
    bool rule_backed_off = false;
    const auto candidates =
        rules[rule_index].lhs.is_var()
            ? graph.classes()
            : graph.classes_for_op(rules[rule_index].lhs.op,
                                   rules[rule_index].lhs.children.size());
    for (Id id : candidates) {
      if (timed_out()) {
        result.stop = StopReason::TimeLimit;
        break;
      }
      const bool completed = search_matches(
          graph, rules[rule_index].lhs, id,
          [&](const Substitution &subst) {
            if (options.per_rule_match_limit &&
                rule_matches >= rule_budgets[rule_index]) {
              rule_backed_off = true;
              return false;
            }
            if (options.match_limit && result.matches >= *options.match_limit) {
              result.stop = StopReason::MatchLimit;
              return false;
            }
            ++rule_matches;
            ++result.matches;
            if (rules[rule_index].condition) {
              ++result.condition_checks;
              const bool allowed =
                  rules[rule_index].condition->check(graph, id, subst);
              if (timed_out()) {
                result.stop = StopReason::TimeLimit;
                return false;
              }
              if (!allowed) {
                ++result.condition_rejections;
                return true;
              }
            }
            result.pending.push_back({rule_index, id, subst});
            return true;
          },
          timed_out);
      if (!completed) {
        if (!rule_backed_off && !result.stop)
          result.stop = StopReason::TimeLimit;
        break;
      }
    }
    if (rule_backed_off) {
      result.any_backoff = true;
      ++result.backed_off_rules;
      const auto max = std::numeric_limits<std::size_t>::max();
      rule_budgets[rule_index] = rule_budgets[rule_index] > max / 2
                                     ? max
                                     : rule_budgets[rule_index] * 2;
    } else if (options.per_rule_match_limit) {
      rule_budgets[rule_index] = *options.per_rule_match_limit;
    }
    if (result.stop)
      break;
  }
  if (timed_out() && result.stop != StopReason::MatchLimit)
    result.stop = StopReason::TimeLimit;
  return result;
}

std::optional<StopReason>
apply_matches(EGraph &graph, const std::vector<Rewrite> &rules,
              const std::vector<PendingMatch> &pending,
              const RunOptions &options, IterationStats &stats,
              const std::function<bool()> &timed_out) {
  for (const auto &item : pending) {
    if (timed_out())
      return StopReason::TimeLimit;
    const Id target = graph.find(item.target);
    const Id rhs =
        instantiate(graph, rules[item.rule_index].rhs, item.substitution);
    ++stats.applications;
    if (graph.merge(target, rhs))
      ++stats.rewrite_unions;
    if (graph.node_count() >= options.node_limit)
      return StopReason::NodeLimit;
  }
  return std::nullopt;
}
} // namespace

RunReport run(EGraph &graph, const std::vector<Rewrite> &rules,
              const RunOptions &options) {
  validate_rules(rules);
  if (options.per_rule_match_limit && *options.per_rule_match_limit == 0)
    throw std::invalid_argument("per-rule match limit must be positive");
  const auto start = Clock::now();
  const auto timed_out = [&] {
    return options.time_limit && Clock::now() - start >= *options.time_limit;
  };

  graph.rebuild();
  RunReport report;
  report.nodes = graph.node_count();
  if (timed_out()) {
    report.reason = StopReason::TimeLimit;
    return report;
  }
  if (report.nodes >= options.node_limit) {
    report.reason = StopReason::NodeLimit;
    return report;
  }

  std::vector<std::size_t> rule_budgets(
      rules.size(), options.per_rule_match_limit.value_or(0));
  for (std::size_t iteration = 0; iteration < options.iteration_limit;
       ++iteration) {
    ++report.iterations;
    IterationStats stats;
    const std::uint64_t revision_before = graph.revision();
    const std::uint64_t analysis_revision_before = graph.analysis_revision();
    const auto search_start = Clock::now();
    const auto search =
        search_iteration(graph, rules, options, rule_budgets, timed_out);
    stats.search_time = Clock::now() - search_start;
    stats.matches = search.matches;
    stats.condition_checks = search.condition_checks;
    stats.condition_rejections = search.condition_rejections;
    stats.backed_off_rules = search.backed_off_rules;
    if (search.stop) {
      report.reason = *search.stop;
      stats.nodes = graph.node_count();
      stats.classes = graph.class_count();
      report.nodes = stats.nodes;
      report.history.push_back(stats);
      return report;
    }

    const auto apply_start = Clock::now();
    const auto apply_stop =
        apply_matches(graph, rules, search.pending, options, stats, timed_out);
    stats.apply_time = Clock::now() - apply_start;

    const std::size_t classes_before_rebuild = graph.class_count();
    const auto rebuild_start = Clock::now();
    graph.rebuild();
    stats.rebuild_time = Clock::now() - rebuild_start;
    const std::size_t classes_after_rebuild = graph.class_count();
    stats.rebuild_unions = classes_before_rebuild - classes_after_rebuild;
    stats.analysis_changes = static_cast<std::size_t>(
        graph.analysis_revision() - analysis_revision_before);
    stats.nodes = graph.node_count();
    stats.classes = classes_after_rebuild;
    stats.completed = !apply_stop;
    report.nodes = stats.nodes;
    report.history.push_back(stats);

    if (apply_stop == StopReason::NodeLimit ||
        report.nodes >= options.node_limit) {
      report.reason = StopReason::NodeLimit;
      return report;
    }
    if (apply_stop == StopReason::TimeLimit || timed_out()) {
      report.reason = StopReason::TimeLimit;
      return report;
    }
    if (graph.revision() == revision_before && !search.any_backoff) {
      report.reason = StopReason::Saturated;
      return report;
    }
  }

  report.reason = StopReason::IterationLimit;
  report.nodes = graph.node_count();
  return report;
}

RunReport run(EGraph &graph, const std::vector<Rewrite> &rules,
              std::size_t iteration_limit, std::size_t node_limit) {
  RunOptions options;
  options.iteration_limit = iteration_limit;
  options.node_limit = node_limit;
  return run(graph, rules, options);
}
} // namespace eggc
