#include "eggc/runner.hpp"
#include <chrono>
#include <set>
#include <stdexcept>
#include <utility>

namespace eggc {
namespace {
using Clock = std::chrono::steady_clock;

void collect_variables(const Pattern& pattern, std::set<std::string>& variables,
                       const std::string& rule_name, const char* side) {
    if (pattern.is_var()) {
        if (!pattern.children.empty())
            throw std::invalid_argument("rewrite '" + rule_name + "': variable " + pattern.op +
                                        " has children in " + side);
        variables.insert(pattern.op);
        return;
    }
    for (const auto& child : pattern.children)
        collect_variables(child, variables, rule_name, side);
}

void validate_rules(const std::vector<Rewrite>& rules) {
    for (const auto& rule : rules) {
        std::set<std::string> lhs_variables;
        std::set<std::string> rhs_variables;
        collect_variables(rule.lhs, lhs_variables, rule.name, "lhs");
        collect_variables(rule.rhs, rhs_variables, rule.name, "rhs");
        for (const auto& variable : rhs_variables)
            if (!lhs_variables.count(variable))
                throw std::invalid_argument("rewrite '" + rule.name +
                                            "' has unbound rhs variable " + variable);
    }
}
}

RunReport run(EGraph& graph, const std::vector<Rewrite>& rules, const RunOptions& options) {
    validate_rules(rules);
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

    for (std::size_t iteration = 0; iteration < options.iteration_limit; ++iteration) {
        ++report.iterations;
        IterationStats stats;
        const std::uint64_t revision_before = graph.revision();
        std::vector<std::pair<std::size_t, std::pair<Id, Substitution>>> pending;
        bool match_limit_hit = false;
        bool search_complete = true;
        const auto search_start = Clock::now();

        for (std::size_t rule_index = 0; rule_index < rules.size() && search_complete; ++rule_index) {
            for (Id id : graph.classes()) {
                if (timed_out()) { search_complete = false; break; }
                const bool completed = search_matches(
                    graph, rules[rule_index].lhs, id,
                    [&](const Substitution& subst) {
                        if (options.match_limit && stats.matches >= *options.match_limit) {
                            match_limit_hit = true;
                            return false;
                        }
                        pending.push_back({rule_index, {id, subst}});
                        ++stats.matches;
                        return true;
                    }, timed_out);
                if (!completed) { search_complete = false; break; }
            }
        }
        stats.search_time = Clock::now() - search_start;

        if (match_limit_hit || timed_out() || !search_complete) {
            report.reason = match_limit_hit ? StopReason::MatchLimit : StopReason::TimeLimit;
            stats.nodes = graph.node_count();
            stats.classes = graph.class_count();
            report.nodes = stats.nodes;
            report.history.push_back(stats);
            return report;
        }

        const auto apply_start = Clock::now();
        bool node_limit_hit = false;
        bool time_limit_hit = false;
        for (const auto& item : pending) {
            if (timed_out()) { time_limit_hit = true; break; }
            const std::size_t rule_index = item.first;
            const Id target = graph.find(item.second.first);
            const Id rhs = instantiate(graph, rules[rule_index].rhs, item.second.second);
            ++stats.applications;
            if (graph.merge(target, rhs)) ++stats.rewrite_unions;
            if (graph.node_count() >= options.node_limit) { node_limit_hit = true; break; }
        }
        stats.apply_time = Clock::now() - apply_start;

        const std::size_t classes_before_rebuild = graph.class_count();
        const auto rebuild_start = Clock::now();
        graph.rebuild();
        stats.rebuild_time = Clock::now() - rebuild_start;
        const std::size_t classes_after_rebuild = graph.class_count();
        stats.rebuild_unions = classes_before_rebuild - classes_after_rebuild;
        stats.nodes = graph.node_count();
        stats.classes = classes_after_rebuild;
        stats.completed = !node_limit_hit && !time_limit_hit;
        report.nodes = stats.nodes;
        report.history.push_back(stats);

        if (node_limit_hit || report.nodes >= options.node_limit) {
            report.reason = StopReason::NodeLimit;
            return report;
        }
        if (time_limit_hit || timed_out()) {
            report.reason = StopReason::TimeLimit;
            return report;
        }
        if (graph.revision() == revision_before) {
            report.reason = StopReason::Saturated;
            return report;
        }
    }

    report.reason = StopReason::IterationLimit;
    report.nodes = graph.node_count();
    return report;
}

RunReport run(EGraph& graph, const std::vector<Rewrite>& rules,
              std::size_t iteration_limit, std::size_t node_limit) {
    RunOptions options;
    options.iteration_limit = iteration_limit;
    options.node_limit = node_limit;
    return run(graph, rules, options);
}
}
