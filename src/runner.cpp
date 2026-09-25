#include "eggc/runner.hpp"

namespace eggc {
RunReport run(EGraph& graph, const std::vector<Rewrite>& rules,
              std::size_t iteration_limit, std::size_t node_limit) {
    for (std::size_t iteration = 0; iteration < iteration_limit; ++iteration) {
        bool changed = false;
        for (const auto& rule : rules) {
            for (Id id : graph.classes()) {
                for (const auto& subst : match(graph, rule.lhs, id)) {
                    Id rhs = instantiate(graph, rule.rhs, subst);
                    changed = graph.merge(id, rhs) || changed;
                }
            }
        }
        graph.rebuild();
        if (graph.node_count() >= node_limit)
            return {StopReason::NodeLimit, iteration + 1, graph.node_count()};
        if (!changed) return {StopReason::Saturated, iteration + 1, graph.node_count()};
    }
    return {StopReason::IterationLimit, iteration_limit, graph.node_count()};
}
}
