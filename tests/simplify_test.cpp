#include "eggc/extract.hpp"
#include "eggc/runner.hpp"
#include <iostream>

int main() {
    eggc::EGraph graph;
    const auto x = graph.add("x");
    const auto zero = graph.add("0");
    const auto one = graph.add("1");
    const auto sum = graph.add("+", {x, zero});
    const auto input = graph.add("*", {sum, one});
    graph.rebuild();

    const auto variable = eggc::Pattern::var("x");
    const std::vector<eggc::Rewrite> rules{
        {"add-zero", eggc::Pattern::node("+", {variable, eggc::Pattern::node("0")}), variable},
        {"mul-one", eggc::Pattern::node("*", {variable, eggc::Pattern::node("1")}), variable},
    };
    const auto report = eggc::run(graph, rules);
    if (report.reason != eggc::StopReason::Saturated) {
        std::cerr << "Expected saturation\n";
        return 1;
    }
    if (graph.find(input) != graph.find(x)) {
        std::cerr << "Expected input and x to be equivalent\n";
        return 1;
    }
    const auto result = eggc::to_string(eggc::extract(graph, input));
    if (result != "x") {
        std::cerr << "Expected x, got " << result << '\n';
        return 1;
    }
    return 0;
}
