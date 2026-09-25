#include "eggc/extract.hpp"
#include "eggc/runner.hpp"
#include <iostream>

using namespace eggc;
int main() {
    EGraph graph;
    Id x = graph.add("x");
    Id zero = graph.add("0");
    Id one = graph.add("1");
    Id sum = graph.add("+", {x, zero});
    Id input = graph.add("*", {sum, one});
    graph.rebuild();

    const auto v = [](const char* name) { return Pattern::var(name); };
    std::vector<Rewrite> rules{
        {"add-zero", Pattern::node("+", {v("x"), Pattern::node("0")}), v("x")},
        {"mul-one", Pattern::node("*", {v("x"), Pattern::node("1")}), v("x")},
    };
    const RunReport report = run(graph, rules);
    std::cout << "simplified: " << to_string(extract(graph, input)) << '\n';
    std::cout << "iterations: " << report.iterations << ", nodes: " << report.nodes << '\n';
}
