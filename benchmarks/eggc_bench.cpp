#include "eggc/parser.hpp"
#include "eggc/testing.hpp"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {
using Clock = std::chrono::steady_clock;
using namespace eggc;

template<class F>
std::chrono::nanoseconds measure(F&& fn) {
    const auto start = Clock::now();
    fn();
    return Clock::now() - start;
}

void print(const char* name, std::size_t nodes, std::size_t classes,
           std::chrono::nanoseconds total, std::chrono::nanoseconds search = {}) {
    std::cout << name << ',' << nodes << ',' << classes << ',' << total.count()
              << ',' << search.count() << '\n';
}
}

int main(int argc, char** argv) {
    std::size_t scale = 1000;
    if (argc > 1) scale = static_cast<std::size_t>(std::stoull(argv[1]));
    if (scale == 0 || scale > 1000000) {
        std::cerr << "scale must be between 1 and 1000000\n";
        return 2;
    }
    std::cout << "case,nodes,classes,total_ns,search_ns\n";

    EGraph deep;
    auto left = deep.add("left");
    auto right = deep.add("right");
    const auto left_leaf = left;
    const auto right_leaf = right;
    for (std::size_t i = 0; i < scale; ++i) {
        left = deep.add("f", {left});
        right = deep.add("f", {right});
    }
    deep.merge(left_leaf, right_leaf);
    const auto deep_time = measure([&] { deep.rebuild(); });
    print("deep_congruence", deep.node_count(), deep.class_count(), deep_time);

    EGraph deep_reference;
    left = deep_reference.add("left");
    right = deep_reference.add("right");
    const auto reference_left_leaf = left;
    const auto reference_right_leaf = right;
    for (std::size_t i = 0; i < scale; ++i) {
        left = deep_reference.add("f", {left});
        right = deep_reference.add("f", {right});
    }
    deep_reference.merge(reference_left_leaf, reference_right_leaf);
    const auto full_scan_time = measure([&] { testing::rebuild_full_scan(deep_reference); });
    print("deep_congruence_full_scan", deep_reference.node_count(),
          deep_reference.class_count(), full_scan_time);

    EGraph unrelated;
    for (std::size_t i = 0; i < scale; ++i) unrelated.add("symbol_" + std::to_string(i));
    unrelated.rebuild();
    const auto rules = std::vector<Rewrite>{
        parse_rewrite("rare-root", "(rare-op ?x)", "?x")
    };
    RunReport run_report;
    const auto runner_time = measure([&] { run_report = run(unrelated, rules); });
    const auto search = run_report.history.empty()
        ? std::chrono::nanoseconds{0} : run_report.history.front().search_time;
    print("many_unrelated_classes", unrelated.node_count(), unrelated.class_count(), runner_time, search);
    const auto scan_search = measure([&] {
        for (const auto id : unrelated.classes())
            (void)search_matches(unrelated, rules.front().lhs, id, [](const Substitution&) {
                return true;
            });
    });
    print("many_unrelated_classes_full_scan_search", unrelated.node_count(),
          unrelated.class_count(), scan_search, scan_search);

    EGraph alternatives;
    const auto a = alternatives.add("a");
    const auto b = alternatives.add("b");
    const auto fa = alternatives.add("f", {a});
    const auto fb = alternatives.add("f", {b});
    alternatives.merge(fa, fb);
    alternatives.rebuild();
    std::size_t match_count = 0;
    const auto match_time = measure([&] {
        match_count = match(alternatives, Pattern::node("f", {Pattern::var("x")}), fa).size();
    });
    print("multi_match", alternatives.node_count(), alternatives.class_count(), match_time);
    std::cerr << "multi_match_results=" << match_count << '\n';
}
