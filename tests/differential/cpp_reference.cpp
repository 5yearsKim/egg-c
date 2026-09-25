#include "eggc/extract.hpp"
#include "eggc/parser.hpp"
#include "eggc/runner.hpp"
#include <iostream>

int main() {
    eggc::EGraph graph;
    const auto root = graph.add_expr(eggc::parse_expr("(+ (* 2 3) (+ x 0))"));
    const auto expected = graph.add_expr(eggc::parse_expr("(+ (* 2 3) x)"));
    const std::vector<eggc::Rewrite> rules{
        eggc::parse_rewrite("add-zero", "(+ ?x 0)", "?x"),
        eggc::parse_rewrite("mul-one", "(* ?x 1)", "?x")
    };
    eggc::RunOptions options;
    options.iteration_limit = 8;
    const auto report = eggc::run(graph, rules, options);
    if (report.reason != eggc::StopReason::Saturated) return 2;
    const auto best = eggc::Extractor(graph).find_best_rec_expr(root);
    std::cout << "equivalent=" << (graph.find(root) == graph.find(expected))
              << " cost=" << best.first << '\n';
}
