#include "eggc/extract.hpp"
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>

namespace {
bool check(bool condition, const char* message) {
    if (condition) return true;
    std::cerr << "FAILED: " << message << '\n';
    return false;
}

bool selects_minimum_and_reuses_choices() {
    eggc::EGraph graph;
    const auto a = graph.add("a");
    const auto long_expr = graph.add("f", {graph.add("g", {a})});
    const auto short_expr = graph.add("b");
    graph.merge(long_expr, short_expr);
    graph.rebuild();
    std::size_t calls = 0;
    eggc::CostPolicy policy = [&](const eggc::ENode& node, const std::vector<std::size_t>& children)
        -> std::optional<std::size_t> {
        ++calls;
        return 1 + (node.op == "b" ? 0 : 10) +
               std::accumulate(children.begin(), children.end(), std::size_t{0});
    };
    eggc::Extractor extractor(graph, policy);
    const auto call_count = calls;
    const auto first = extractor.find_best(long_expr);
    const auto second = extractor.find_best(short_expr);
    return check(first.expression.op == "b" && second.expression.op == "b",
                 "all roots use their best class representative") &&
           check(first.cost == 1 && second.cost == 1, "best cost is returned") &&
           check(calls == call_count, "queries reuse the constructor's cost analysis");
}

bool built_in_costs_and_cycles_work() {
    eggc::EGraph graph;
    const auto a = graph.add("a");
    const auto f = graph.add("f", {a});
    graph.merge(a, f);
    graph.rebuild();
    eggc::Extractor size(graph);
    eggc::Extractor depth(graph, eggc::ast_depth_cost());
    const auto size_result = size.find_best(a);
    const auto depth_result = depth.find_best(f);
    return check(size_result.cost == 1 && size_result.expression.op == "a",
                 "AST size chooses the leaf in a cycle") &&
           check(depth_result.cost == 1 && depth_result.expression.op == "a",
                 "AST depth chooses a finite leaf in a cycle");
}

bool stale_and_dirty_graphs_are_rejected() {
    eggc::EGraph graph;
    const auto a = graph.add("a");
    graph.rebuild();
    eggc::Extractor stale(graph);
    graph.add("b");
    bool stale_rejected = false;
    try { (void)stale.best_cost(a); }
    catch (const std::logic_error&) { stale_rejected = true; }

    bool dirty_rejected = false;
    try { eggc::Extractor invalid(graph); }
    catch (const std::logic_error&) { dirty_rejected = true; }
    return check(stale_rejected, "mutated graph invalidates a cached extractor") &&
           check(dirty_rejected, "dirty graphs cannot construct an extractor");
}

bool overflowed_candidates_are_skipped() {
    eggc::EGraph graph;
    const auto a = graph.add("a");
    graph.add("f", {a});
    graph.rebuild();
    eggc::CostPolicy policy = [](const eggc::ENode& node, const std::vector<std::size_t>&)
        -> std::optional<std::size_t> {
        if (node.op == "a") return 1;
        return std::nullopt;
    };
    eggc::Extractor extractor(graph, policy);
    return check(extractor.best_cost(a) == 1, "unrepresentable candidates are ignored");
}

bool built_in_cost_detects_size_overflow() {
    const auto cost = eggc::ast_size_cost()(eggc::ENode{"f", {}},
        {std::numeric_limits<std::size_t>::max()});
    return check(!cost, "AST-size arithmetic overflow is reported as no finite cost");
}

bool flat_reconstruction_preserves_sharing_and_handles_depth() {
    eggc::EGraph graph;
    const auto a = graph.add("a");
    const auto fa = graph.add("f", {a});
    const auto pair = graph.add("pair", {fa, fa});
    graph.rebuild();
    const auto flat = eggc::Extractor(graph).find_best_rec_expr(pair).second;
    if (!check(flat.nodes.size() == 3, "flat output stores a shared child class once") ||
        !check(flat.nodes.back().children[0] == flat.nodes.back().children[1],
               "both parent edges refer to the shared child")) return false;

    std::string deep = "leaf";
    for (int i = 0; i < 1500; ++i) deep = "(f " + deep + ")";
    const auto parsed = eggc::parse_expr(deep);
    return check(parsed.nodes.size() == 1501, "deep S-expressions parse iteratively") &&
           check(eggc::to_string(parsed) == deep, "deep flat expressions print iteratively");
}
}

int main() {
    const bool passed = selects_minimum_and_reuses_choices() &&
                        built_in_costs_and_cycles_work() &&
                        stale_and_dirty_graphs_are_rejected() &&
                        overflowed_candidates_are_skipped() &&
                        built_in_cost_detects_size_overflow() &&
                        flat_reconstruction_preserves_sharing_and_handles_depth();
    return passed ? 0 : 1;
}
