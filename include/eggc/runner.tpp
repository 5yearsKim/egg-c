#pragma once

#include <chrono>
#include <functional>
#include <limits>
#include <stdexcept>
#include <utility>

namespace eggc {
namespace runner_detail {
using Clock = std::chrono::steady_clock;

template <Language L, class A>
  requires AnalysisFor<A, L>
void validate_rules(const std::vector<Rewrite<L, A>>& rules) {
  for (const auto& rule : rules) validate_rewrite(rule);
}

template <Language L, class A>
  requires AnalysisFor<A, L>
struct SearchResult {
  std::vector<Application<L, A>> pending;
  std::size_t matches = 0;
  std::size_t condition_checks = 0;
  std::size_t condition_rejections = 0;
  std::size_t backed_off_rules = 0;
  bool any_backoff = false;
  std::optional<StopReason> stop;
};

template <Language L, class A>
  requires AnalysisFor<A, L>
SearchResult<L, A> search_iteration(const EGraph<L, A>& graph,
                                    const std::vector<Rewrite<L, A>>& rules,
                                    const RunOptions& options,
                                    std::vector<std::size_t>& rule_budgets,
                                    const std::function<bool()>& timed_out) {
  SearchResult<L, A> result;
  for (std::size_t rule_index = 0; rule_index < rules.size(); ++rule_index) {
    std::size_t rule_matches = 0;
    bool rule_backed_off = false;
    const auto accept = [&](Application<L, A> action, bool allowed) {
      if (timed_out()) {
        result.stop = StopReason::TimeLimit;
        return false;
      }
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
      if (allowed) {
        graph.find(action.target);
        if (!action.apply)
          throw std::invalid_argument("empty rewrite application");
        result.pending.push_back(std::move(action));
      }
      return true;
    };
    const auto& rule = rules[rule_index];
    bool completed = true;
    if (rule.custom_search) {
      completed = rule.custom_search(
          graph,
          [&](Application<L, A> action) {
            const auto condition = action.condition_result;
            const bool keep_going = accept(std::move(action), condition.value_or(true));
            if (keep_going && condition) {
              ++result.condition_checks;
              if (!*condition) ++result.condition_rejections;
            }
            return keep_going;
          },
          timed_out);
    } else {
      const auto* root_node = std::get_if<L>(&rule.lhs->nodes.back());
      const auto candidates =
          root_node ? graph.classes_for_op(root_node->discriminant())
                    : graph.classes();
      for (Id id : candidates) {
        completed = search_matches(
            graph, *rule.lhs, id,
            [&](const Substitution& subst) {
              // Check budgets before invoking potentially expensive conditions.
              bool allowed = true;
              if ((!options.per_rule_match_limit ||
                   rule_matches < rule_budgets[rule_index]) &&
                  (!options.match_limit ||
                   result.matches < *options.match_limit) &&
                  rule.condition) {
                ++result.condition_checks;
                allowed = rule.condition->check(graph, id, subst);
                if (!allowed) ++result.condition_rejections;
              }
              const auto rhs = *rule.rhs;
              return accept(
                  {id,
                   [rhs, subst](EGraph<L, A>& target) -> std::optional<Id> {
                     return instantiate(target, rhs, subst);
                   }},
                  allowed);
            },
            timed_out);
        if (!completed) break;
      }
    }
    if (!completed && !rule_backed_off && !result.stop)
      result.stop = timed_out() ? StopReason::TimeLimit : StopReason::SearchLimit;
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
    if (result.stop) break;
  }
  if (timed_out() && result.stop != StopReason::MatchLimit)
    result.stop = StopReason::TimeLimit;
  return result;
}

template <Language L, class A>
  requires AnalysisFor<A, L>
std::optional<StopReason> apply_matches(
    EGraph<L, A>& graph, const std::vector<Application<L, A>>& pending,
    const RunOptions& options, IterationStats& stats,
    const std::function<bool()>& timed_out) {
  for (const auto& item : pending) {
    if (timed_out()) return StopReason::TimeLimit;
    const Id target = graph.find(item.target);
    const auto rhs = item.apply(graph);
    ++stats.applications;
    if (rhs && graph.merge(target, *rhs)) ++stats.rewrite_unions;
    if (graph.node_count() >= options.node_limit) return StopReason::NodeLimit;
  }
  return std::nullopt;
}
}  // namespace runner_detail

template <Language L, class A>
  requires AnalysisFor<A, L>
RunReport run(EGraph<L, A>& graph, const std::vector<Rewrite<L, A>>& rules,
              const RunOptions& options) {
  runner_detail::validate_rules(rules);
  if (options.per_rule_match_limit && *options.per_rule_match_limit == 0)
    throw std::invalid_argument("per-rule match limit must be positive");
  const auto start = runner_detail::Clock::now();
  const auto timed_out = [&] {
    return options.time_limit &&
           runner_detail::Clock::now() - start >= *options.time_limit;
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
    const auto search_start = runner_detail::Clock::now();
    const auto search = runner_detail::search_iteration(
        graph, rules, options, rule_budgets, timed_out);
    stats.search_time = runner_detail::Clock::now() - search_start;
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

    const auto apply_start = runner_detail::Clock::now();
    const auto apply_stop = runner_detail::apply_matches(
        graph, search.pending, options, stats, timed_out);
    stats.apply_time = runner_detail::Clock::now() - apply_start;

    const std::size_t classes_before_rebuild = graph.class_count();
    const auto rebuild_start = runner_detail::Clock::now();
    graph.rebuild();
    stats.rebuild_time = runner_detail::Clock::now() - rebuild_start;
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

template <Language L, class A>
  requires AnalysisFor<A, L>
RunReport run(EGraph<L, A>& graph, const std::vector<Rewrite<L, A>>& rules,
              std::size_t iteration_limit, std::size_t node_limit) {
  RunOptions options;
  options.iteration_limit = iteration_limit;
  options.node_limit = node_limit;
  return run(graph, rules, options);
}
}  // namespace eggc
