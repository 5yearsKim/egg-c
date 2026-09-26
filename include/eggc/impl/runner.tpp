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

struct PatternApplication {
  Id target;
  std::vector<Id> bindings;
};
template <Language L, class A>
  requires AnalysisFor<A, L>
struct PendingApplication {
  std::variant<Application<L, A>, PatternApplication> action;
  std::size_t rule_index;
};

template <Language L, class A>
  requires AnalysisFor<A, L>
struct SearchResult {
  std::vector<PendingApplication<L, A>> pending;
  std::vector<RuleStats> rules;
  std::size_t matches = 0;
  std::size_t condition_checks = 0;
  std::size_t condition_rejections = 0;
  std::size_t backed_off_rules = 0;
  bool any_backoff = false;
  std::optional<StopReason> stop;
};

template <Language L, class A>
  requires AnalysisFor<A, L>
SearchResult<L, A> search_iteration(
    const EGraph<L, A>& graph, const std::vector<Rewrite<L, A>>& rules,
    const std::vector<std::optional<CompiledPattern<L>>>& compiled_lhs,
    const RunOptions& options, std::vector<std::size_t>& rule_budgets,
    const std::function<bool()>& timed_out) {
  SearchResult<L, A> result;
  if (options.collect_rule_stats) {
    result.rules.resize(rules.size());
    for (std::size_t i = 0; i < rules.size(); ++i) {
      result.rules[i].rule_index = i;
      result.rules[i].name = rules[i].name;
    }
  }
  for (std::size_t rule_index = 0; rule_index < rules.size(); ++rule_index) {
    const auto pending_start = result.pending.size();
    const auto checks_before = result.condition_checks;
    const auto rejections_before = result.condition_rejections;
    const auto search_start =
        options.collect_rule_stats ? Clock::now() : Clock::time_point{};
    std::size_t rule_matches = 0;
    bool rule_backed_off = false;
    const auto accept = [&](auto action, bool allowed) {
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
        if constexpr (std::same_as<decltype(action), Application<L, A>>) {
          if (!action.apply)
            throw std::invalid_argument("empty rewrite application");
        }
        result.pending.push_back({std::move(action), rule_index});
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
            const bool keep_going =
                accept(std::move(action), condition.value_or(true));
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
        completed = compiled_lhs[rule_index]->search(
            graph, id,
            [&](const std::vector<Id>& bindings) {
              // Check budgets before invoking potentially expensive conditions.
              bool allowed = true;
              if ((!options.per_rule_match_limit ||
                   rule_matches < rule_budgets[rule_index]) &&
                  (!options.match_limit ||
                   result.matches < *options.match_limit) &&
                  rule.condition) {
                ++result.condition_checks;
                allowed = rule.condition->check(
                    graph, id,
                    compiled_lhs[rule_index]->substitution(bindings));
                if (!allowed) ++result.condition_rejections;
              }
              return accept(PatternApplication{id, bindings}, allowed);
            },
            timed_out);
        if (!completed) break;
      }
    }
    if (!completed && !rule_backed_off && !result.stop)
      result.stop =
          timed_out() ? StopReason::TimeLimit : StopReason::SearchLimit;
    if (rule_backed_off) {
      result.pending.resize(pending_start);
      result.any_backoff = true;
      ++result.backed_off_rules;
      const auto max = std::numeric_limits<std::size_t>::max();
      rule_budgets[rule_index] = rule_budgets[rule_index] > max / 2
                                     ? max
                                     : rule_budgets[rule_index] * 2;
    } else if (options.per_rule_match_limit) {
      rule_budgets[rule_index] = *options.per_rule_match_limit;
    }
    if (options.collect_rule_stats) {
      auto& stats = result.rules[rule_index];
      stats.searched = true;
      stats.search_completed = completed;
      stats.backed_off = rule_backed_off;
      stats.matches = rule_matches;
      stats.condition_checks = result.condition_checks - checks_before;
      stats.condition_rejections =
          result.condition_rejections - rejections_before;
      stats.search_time = Clock::now() - search_start;
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
    EGraph<L, A>& graph, const std::vector<PendingApplication<L, A>>& pending,
    const std::vector<std::optional<CompiledPattern<L>>>& compiled_rhs,
    const std::vector<Rewrite<L, A>>& rules, const RunOptions& options,
    IterationStats& stats, const std::function<bool()>& timed_out) {
  for (const auto& item : pending) {
    if (timed_out()) return StopReason::TimeLimit;
    const auto apply_start =
        options.collect_rule_stats ? Clock::now() : Clock::time_point{};
    const Id target = std::visit(
        [](const auto& action) { return action.target; }, item.action);
    const auto rhs = std::visit(
        [&](const auto& action) -> std::optional<Id> {
          if constexpr (std::same_as<std::decay_t<decltype(action)>,
                                     PatternApplication>)
            return compiled_rhs[item.rule_index]->instantiate(graph,
                                                              action.bindings);
          else
            return action.apply(graph);
        },
        item.action);
    ++stats.applications;
    const bool united =
        rhs && (graph.explanations_enabled()
                    ? graph.merge(
                          target, *rhs,
                          {UnionKind::Rewrite, rules[item.rule_index].name, {}})
                    : graph.merge(target, *rhs));
    if (united) ++stats.rewrite_unions;
    if (options.collect_rule_stats) {
      auto& rule = stats.rules[item.rule_index];
      ++rule.applications;
      if (united) ++rule.rewrite_unions;
      rule.apply_time += Clock::now() - apply_start;
    }
    if (graph.node_count() >= options.node_limit) return StopReason::NodeLimit;
  }
  return std::nullopt;
}
}  // namespace runner_detail

template <Language L, class A>
  requires AnalysisFor<A, L>
RunReport run(EGraph<L, A>& graph, const std::vector<Rewrite<L, A>>& rules,
              const RunOptions& options,
              const std::vector<IterationHook<L, A>>& hooks) {
  // Patterns and callbacks are snapshotted for the lifetime of this run.
  const auto frozen_rules = rules;
  const auto frozen_hooks = hooks;
  for (const auto& hook : frozen_hooks)
    if (!hook) throw std::invalid_argument("empty iteration hook");
  runner_detail::validate_rules(frozen_rules);
  std::vector<std::optional<CompiledPattern<L>>> compiled_lhs, compiled_rhs;
  compiled_lhs.reserve(frozen_rules.size());
  compiled_rhs.reserve(frozen_rules.size());
  for (const auto& rule : frozen_rules) {
    if (rule.custom_search) {
      compiled_lhs.emplace_back();
      compiled_rhs.emplace_back();
    } else {
      compiled_lhs.emplace_back(std::in_place, *rule.lhs);
      compiled_rhs.emplace_back(std::in_place, *rule.rhs,
                                compiled_lhs.back()->variables());
    }
  }
  if (options.per_rule_match_limit && *options.per_rule_match_limit == 0)
    throw std::invalid_argument("per-rule match limit must be positive");
  const auto start = runner_detail::Clock::now();
  const auto timed_out = [&] {
    return options.time_limit &&
           runner_detail::Clock::now() - start >= *options.time_limit;
  };

  std::optional<StopReason> rebuild_stop;
  const auto rebuild_graph = [&] {
    return graph.rebuild([&] {
      if (timed_out()) {
        rebuild_stop = StopReason::TimeLimit;
        return true;
      }
      if (graph.node_count() >= options.node_limit) {
        rebuild_stop = StopReason::NodeLimit;
        return true;
      }
      return false;
    });
  };
  rebuild_graph();
  RunReport report;
  report.initial_rebuild = graph.last_rebuild_stats();
  report.nodes = graph.node_count();
  if (rebuild_stop) {
    report.reason = *rebuild_stop;
    return report;
  }
  if (timed_out()) {
    report.reason = StopReason::TimeLimit;
    return report;
  }
  if (report.nodes >= options.node_limit) {
    report.reason = StopReason::NodeLimit;
    return report;
  }

  std::vector<std::size_t> rule_budgets(
      frozen_rules.size(), options.per_rule_match_limit.value_or(0));
  for (std::size_t iteration = 0; iteration < options.iteration_limit;
       ++iteration) {
    ++report.iterations;
    IterationStats stats;
    const std::uint64_t revision_before = graph.revision();
    const std::uint64_t analysis_revision_before = graph.analysis_revision();
    const auto hook_start = runner_detail::Clock::now();
    for (const auto& hook : frozen_hooks) {
      if (!hook) throw std::invalid_argument("empty iteration hook");
      const bool proceed = hook(graph, report);
      rebuild_graph();
      stats.analysis_evaluations +=
          graph.last_rebuild_stats().analysis_evaluations;
      if (!proceed || rebuild_stop || timed_out() ||
          graph.node_count() >= options.node_limit) {
        report.reason =
            !proceed
                ? StopReason::UserRequested
                : rebuild_stop.value_or(timed_out() ? StopReason::TimeLimit
                                                    : StopReason::NodeLimit);
        stats.hook_time = runner_detail::Clock::now() - hook_start;
        stats.nodes = report.nodes = graph.node_count();
        stats.classes = graph.class_count();
        report.history.push_back(std::move(stats));
        return report;
      }
    }
    stats.hook_time = runner_detail::Clock::now() - hook_start;
    const auto search_start = runner_detail::Clock::now();
    auto search = runner_detail::search_iteration(
        graph, frozen_rules, compiled_lhs, options, rule_budgets, timed_out);
    stats.search_time = runner_detail::Clock::now() - search_start;
    stats.matches = search.matches;
    stats.condition_checks = search.condition_checks;
    stats.condition_rejections = search.condition_rejections;
    stats.backed_off_rules = search.backed_off_rules;
    stats.rules = std::move(search.rules);
    if (search.stop) {
      report.reason = *search.stop;
      stats.nodes = graph.node_count();
      stats.classes = graph.class_count();
      report.nodes = stats.nodes;
      report.history.push_back(std::move(stats));
      return report;
    }

    const auto apply_start = runner_detail::Clock::now();
    const auto apply_stop =
        runner_detail::apply_matches(graph, search.pending, compiled_rhs,
                                     frozen_rules, options, stats, timed_out);
    stats.apply_time = runner_detail::Clock::now() - apply_start;

    const auto rebuild_start = runner_detail::Clock::now();
    rebuild_graph();
    stats.analysis_evaluations +=
        graph.last_rebuild_stats().analysis_evaluations;
    stats.rebuild_time = runner_detail::Clock::now() - rebuild_start;
    const std::size_t classes_after_rebuild = graph.class_count();
    stats.rebuild_unions = graph.last_rebuild_stats().congruence_unions +
                           graph.last_rebuild_stats().analysis_unions;
    stats.analysis_changes = static_cast<std::size_t>(
        graph.analysis_revision() - analysis_revision_before);
    stats.nodes = graph.node_count();
    stats.classes = classes_after_rebuild;
    stats.completed = !apply_stop;
    report.nodes = stats.nodes;
    report.history.push_back(std::move(stats));

    if (rebuild_stop) {
      report.reason = *rebuild_stop;
      return report;
    }
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
