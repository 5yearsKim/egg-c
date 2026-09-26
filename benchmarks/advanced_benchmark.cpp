#include <chrono>
#include <cstdlib>
#include <eggc/all.hpp>
#include <iostream>
#include <new>
#include <string>
#include <string_view>

// Benchmark-only interception counts allocation requests, not resident memory.
namespace allocations {
bool enabled = false;
std::size_t calls = 0, bytes = 0;
}  // namespace allocations
void* operator new(std::size_t size) {
  if (allocations::enabled) {
    ++allocations::calls;
    allocations::bytes += size;
  }
  if (void* memory = std::malloc(size ? size : 1)) return memory;
  throw std::bad_alloc();
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
namespace {
using Node = eggc::SymbolLang;
using Graph = eggc::EGraph<Node>;
using Clock = std::chrono::steady_clock;
void measure(std::string_view workload, std::size_t size,
             std::size_t repetition) {
  Graph graph;
  std::vector<eggc::Id> roots, leaves;
  for (std::size_t i = 0; i < size; ++i) {
    auto leaf = graph.add(Node::leaf("x" + std::to_string(i)));
    leaves.push_back(leaf);
    if (workload == "wide_dag") {
      if (i) graph.merge(leaves[0], leaf);
    } else
      roots.push_back(graph.add(Node::node(
          workload == "same_operator_update" ? "f" : "pair",
          workload == "same_operator_update" ? std::vector{leaf}
                                             : std::vector{leaf, leaf})));
  }
  graph.rebuild();
  graph.check_invariants();
  eggc::CompiledPattern<Node> matcher(eggc::parse_pattern("(pair ?x ?x)"));
#ifndef EGGC_REVIEW_BASELINE
  eggc::MatcherWorkspace workspace;
  if (workload == "matching_reuse")
    matcher.search(graph, roots.front(), workspace,
                   [](const auto&) { return true; });
  graph.reset_index_stats();
#endif
  if (workload == "same_operator_update") graph.merge(leaves[0], leaves[1]);
  std::size_t matches = 0, cost_calls = 0;
  std::optional<eggc::DagExtractor<Node>> extractor;
  if (workload == "wide_dag")
    extractor.emplace(graph, [&](const Node&) -> std::optional<double> {
      ++cost_calls;
      return 1.;
    });
  allocations::calls = 0;
  allocations::bytes = 0;
  allocations::enabled = true;
  const auto start = Clock::now();
  if (workload == "matching_reuse") {
    for (auto root : roots) {
#ifdef EGGC_REVIEW_BASELINE
      matcher.search(graph, root, [&](const auto&) {
        ++matches;
        return true;
      });
#else
      matcher.search(graph, root, workspace, [&](const auto&) {
        ++matches;
        return true;
      });
#endif
    }
  } else if (workload == "wide_dag") {
    eggc::DagOptions options;
    options.state_limit = 1;
    auto result = extractor->solve(leaves.front(), options);
    if (result.explored_states != 1)
      throw std::logic_error("invalid state count");
  } else
    graph.rebuild();
  const auto elapsed = Clock::now() - start;
  allocations::enabled = false;
#ifndef EGGC_REVIEW_BASELINE
  const auto copied = graph.index_stats().copied_candidate_ids;
#else
  const char* copied =
      "unavailable";  // Baseline did not expose an index counter.
#endif
  std::cout
      << workload << ',' << size << ',' << repetition << ',' << matches << ','
      << cost_calls << ',' << allocations::calls << ',' << allocations::bytes
      << ',' << copied << ','
      << std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count()
      << '\n';
  if (workload == "matching_reuse" && matches != size)
    throw std::logic_error("lost matches");
  graph.check_invariants();
}
}  // namespace
int main(int argc, char** argv) {
  try {
    if (argc > 4)
      throw std::invalid_argument(
          "usage: advanced_benchmark [workload|all] [size] [repetitions]");
    const std::string_view workload = argc > 1 ? argv[1] : "all";
    const auto size = argc > 2 ? std::stoul(argv[2]) : 1000;
    const auto repetitions = argc > 3 ? std::stoul(argv[3]) : 3;
    if (size < 2 || size > 10000 || repetitions < 1 || repetitions > 100)
      throw std::invalid_argument("size 2..10000, repetitions 1..100");
    if (workload != "all" && workload != "matching_reuse" &&
        workload != "wide_dag" && workload != "same_operator_update")
      throw std::invalid_argument("unknown workload");
    std::cout << "workload,size,repetition,matches,cost_calls,allocation_calls,"
                 "requested_bytes,copied_candidate_ids,elapsed_ns\n";
    for (auto name : {"matching_reuse", "wide_dag", "same_operator_update"})
      if (workload == "all" || workload == name)
        for (std::size_t i = 0; i < repetitions; ++i) measure(name, size, i);
  } catch (const std::exception& error) {
    allocations::enabled = false;
    std::cerr << error.what() << '\n';
    return 1;
  }
}
