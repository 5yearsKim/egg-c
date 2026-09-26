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
  std::size_t rule_index = 0;
  PendingApplication() = default;
  template <class Action>
  PendingApplication(Action&& value, std::size_t index)
      : action(std::in_place_type<std::remove_cvref_t<Action>>,
               std::forward<Action>(value)),
        rule_index(index) {}
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
    std::vector<MatcherWorkspace>& workspaces,
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
        result.pending.emplace_back(std::move(action), rule_index);
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
      std::vector<Id> all_classes;
      if (!root_node) all_classes = graph.classes();
      const auto& candidates =
          root_node ? graph.classes_for_op(root_node->discriminant())
                    : all_classes;
      for (Id id : candidates) {
        completed = compiled_lhs[rule_index]->search(
            graph, id, workspaces[rule_index],
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
    const std::vector<std::optional<CompiledReplacement<L>>>& compiled_rhs,
    const std::vector<Rewrite<L, A>>& rules, const RunOptions& options,
    IterationStats& stats, std::vector<Id>& scratch,
    const std::function<bool()>& timed_out) {
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
            return compiled_rhs[item.rule_index]->instantiate(
                graph, action.bindings, scratch);
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
    if (options.memory_limit_bytes &&
        graph.memory_stats().estimated_bytes >= *options.memory_limit_bytes)
      return StopReason::MemoryLimit;
  }
  return std::nullopt;
}
}  // namespace runner_detail

template <Language L, class A>
  requires AnalysisFor<A, L>
CompiledRules<L, A>::CompiledRules(std::vector<Rewrite<L, A>> rules) {
  auto program = std::make_shared<Program>();
  program->rules = std::move(rules);
  runner_detail::validate_rules(program->rules);
  for (const auto& rule : program->rules) {
    if (rule.custom_search) {
      program->lhs.emplace_back();
      program->rhs.emplace_back();
    } else {
      program->lhs.emplace_back(std::in_place, *rule.lhs);
      program->rhs.emplace_back(std::in_place, *rule.rhs,
                                program->lhs.back()->variables());
    }
  }
  program_ = std::move(program);
}

template <Language L, class A>
  requires AnalysisFor<A, L>
Runner<L, A>::Runner(EGraph<L, A>& graph, CompiledRules<L, A> rules,
                     std::vector<IterationHook<L, A>> hooks)
    : graph_(&graph),
      compiled_(std::move(rules)),
      hooks_(std::move(hooks)),
      matcher_workspaces_(compiled_.rules().size()) {
  for (const auto& hook : hooks_)
    if (!hook) throw std::invalid_argument("empty iteration hook");
}

template <Language L, class A>
  requires AnalysisFor<A, L>
const RunReport& Runner<L, A>::resume(const RunOptions& options) {
  if (running_) throw std::logic_error("runner cannot resume recursively");
  struct Guard {
    bool& running;
    explicit Guard(bool& value) : running(value) { running = true; }
    ~Guard() { running = false; }
  } guard(running_);
  if (options.per_rule_match_limit && *options.per_rule_match_limit == 0)
    throw std::invalid_argument("per-rule match limit must be positive");
  if (rule_budgets_.empty() ||
      initial_rule_budget_ != options.per_rule_match_limit) {
    initial_rule_budget_ = options.per_rule_match_limit;
    rule_budgets_.assign(compiled_.rules().size(),
                         options.per_rule_match_limit.value_or(0));
  }
  auto& graph = *graph_;
  auto& report = report_;
  const auto start = runner_detail::Clock::now();
  const auto timed_out = [&] {
    return options.time_limit &&
           runner_detail::Clock::now() - start >= *options.time_limit;
  };
  const auto limit = [&]() -> std::optional<StopReason> {
    if (timed_out()) return StopReason::TimeLimit;
    if (graph.node_count() >= options.node_limit) return StopReason::NodeLimit;
    if (options.memory_limit_bytes &&
        graph.memory_stats().estimated_bytes >= *options.memory_limit_bytes)
      return StopReason::MemoryLimit;
    return {};
  };
  std::optional<StopReason> rebuild_stop;
  const auto rebuild_graph = [&] {
    graph.rebuild([&] {
      auto why = limit();
      if (why) rebuild_stop = why;
      return why.has_value();
    });
  };
  rebuild_graph();
  if (report.preparation_rebuilds.empty())
    report.initial_rebuild = graph.last_rebuild_stats();
  report.preparation_rebuilds.push_back(graph.last_rebuild_stats());
  report.nodes = graph.node_count();
  if (auto why = rebuild_stop ? rebuild_stop : limit()) {
    report.reason = *why;
    return report;
  }
  for (std::size_t iteration = 0; iteration < options.iteration_limit;
       ++iteration) {
    ++report.iterations;
    IterationStats stats;
    const auto revision_before = graph.revision();
    const auto analysis_before = graph.analysis_revision();
    const auto finish = [&](StopReason reason,
                            bool completed = false) -> const RunReport& {
      stats.analysis_changes =
          static_cast<std::size_t>(graph.analysis_revision() - analysis_before);
      stats.nodes = report.nodes = graph.node_count();
      stats.classes = graph.class_count();
      stats.completed = completed;
      report.reason = reason;
      report.history.push_back(std::move(stats));
      return report;
    };
    const auto repair = [&] {
      const auto begin = runner_detail::Clock::now();
      rebuild_graph();
      stats.rebuild_time += runner_detail::Clock::now() - begin;
      const auto& rebuild = graph.last_rebuild_stats();
      stats.analysis_evaluations += rebuild.analysis_evaluations;
      stats.rebuild_unions +=
          rebuild.congruence_unions + rebuild.analysis_unions;
    };
    for (const auto& hook : hooks_) {
      const auto begin = runner_detail::Clock::now();
      const bool proceed = hook(graph, report);
      stats.hook_time += runner_detail::Clock::now() - begin;
      repair();
      if (!proceed) return finish(StopReason::UserRequested);
      if (auto why = rebuild_stop ? rebuild_stop : limit()) return finish(*why);
    }
    const auto search_start = runner_detail::Clock::now();
    auto search = runner_detail::search_iteration(
        graph, compiled_.rules(), compiled_.searchers(), options, rule_budgets_,
        matcher_workspaces_, timed_out);
    stats.search_time = runner_detail::Clock::now() - search_start;
    stats.matches = search.matches;
    stats.condition_checks = search.condition_checks;
    stats.condition_rejections = search.condition_rejections;
    stats.backed_off_rules = search.backed_off_rules;
    stats.rules = std::move(search.rules);
    if (search.stop) return finish(*search.stop);
    const auto apply_start = runner_detail::Clock::now();
    auto apply_stop = runner_detail::apply_matches(
        graph, search.pending, compiled_.replacements(), compiled_.rules(),
        options, stats, replacement_scratch_, timed_out);
    stats.apply_time = runner_detail::Clock::now() - apply_start;
    repair();
    if (rebuild_stop) return finish(*rebuild_stop);
    if (apply_stop) return finish(*apply_stop);
    if (auto why = limit()) return finish(*why);
    const bool saturated =
        graph.revision() == revision_before && !search.any_backoff;
    finish(saturated ? StopReason::Saturated : StopReason::IterationLimit,
           true);
    if (saturated) return report;
  }
  report.reason = StopReason::IterationLimit;
  return report;
}

template <Language L, class A>
  requires AnalysisFor<A, L>
RunReport run(EGraph<L, A>& graph, const CompiledRules<L, A>& rules,
              const RunOptions& options,
              const std::vector<IterationHook<L, A>>& hooks) {
  Runner<L, A> runner(graph, rules, hooks);
  return runner.resume(options);
}
template <Language L, class A>
  requires AnalysisFor<A, L>
RunReport run(EGraph<L, A>& graph, const std::vector<Rewrite<L, A>>& rules,
              const RunOptions& options,
              const std::vector<IterationHook<L, A>>& hooks) {
  return run(graph, CompiledRules<L, A>(rules), options, hooks);
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
