#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <eggc/all.hpp>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using Node = eggc::SymbolLang;
using Clock = std::chrono::steady_clock;

struct Facts {
  using Data = unsigned;
  std::shared_ptr<std::size_t> evaluations;
  Data make(const eggc::EGraph<Node, Facts>& graph, const Node& node) const {
    ++*evaluations;
    Data result = node.op == "known" ? 1 : 0;
    for (auto child : node.args) result |= graph.analysis_data(child);
    return result;
  }
  eggc::AnalysisMerge merge(Data& into, const Data& from) const {
    if ((into | from) == into) return eggc::AnalysisMerge::Unchanged;
    into |= from;
    return eggc::AnalysisMerge::Changed;
  }
};
using Graph = eggc::EGraph<Node, Facts>;

struct Sample {
  std::string_view workload;
  std::size_t input_size;
  std::size_t nodes;
  std::size_t classes;
  std::size_t evaluations;
  eggc::RunReport report;
  std::chrono::nanoseconds elapsed;
  std::chrono::nanoseconds extraction;
  std::size_t cost;
};

const char* reason(eggc::StopReason value) {
  switch (value) {
    case eggc::StopReason::Saturated:
      return "saturated";
    case eggc::StopReason::IterationLimit:
      return "iteration_limit";
    case eggc::StopReason::NodeLimit:
      return "node_limit";
    case eggc::StopReason::TimeLimit:
      return "time_limit";
    case eggc::StopReason::MatchLimit:
      return "match_limit";
    case eggc::StopReason::UserRequested:
      return "user_requested";
    case eggc::StopReason::MemoryLimit:
      return "memory_limit";
    case eggc::StopReason::SearchLimit:
      return "search_limit";
  }
  throw std::logic_error("unknown stop reason");
}

Sample measure(std::string_view workload, std::size_t size) {
  auto evaluations = std::make_shared<std::size_t>(0);
  Graph graph(Facts{evaluations});
  eggc::Id root = 0;
  std::vector<eggc::Rewrite<Node, Facts>> rules;
  eggc::RunOptions options;
  options.iteration_limit = 4;
  options.node_limit = 100000;
  options.match_limit = 10000;

  if (workload == "analysis_chain") {
    std::vector<eggc::Id> ids;
    for (std::size_t i = 0; i <= size; ++i)
      ids.push_back(graph.add(Node::leaf("x" + std::to_string(i))));
    for (std::size_t i = 0; i < size; ++i)
      graph.merge(ids[i], graph.add(Node::node("f", {ids[i + 1]})));
    graph.rebuild();
    graph.merge(ids.back(), graph.add(Node::leaf("known")));
    root = ids.front();
  } else if (workload == "congruence_cascade") {
    auto a = graph.add(Node::leaf("a"));
    auto b = graph.add(Node::leaf("b"));
    auto lhs = a, rhs = b;
    for (std::size_t i = 0; i < size; ++i) {
      lhs = graph.add(Node::node("f", {lhs}));
      rhs = graph.add(Node::node("f", {rhs}));
    }
    graph.rebuild();
    graph.merge(a, b);
    root = lhs;
  } else if (workload == "small_update") {
    for (std::size_t i = 0; i < size; ++i) {
      auto leaf = graph.add(Node::leaf("unrelated" + std::to_string(i)));
      graph.add(Node::node("g", {leaf}));
    }
    auto a = graph.add(Node::leaf("a"));
    root = graph.add(Node::node("f", {a}));
    graph.rebuild();
    graph.merge(a, graph.add(Node::leaf("known")));
  } else if (workload == "shared_expression") {
    root = graph.add(Node::leaf("a"));
    for (std::size_t i = 0; i < size; ++i)
      root = graph.add(Node::node("pair", {root, root}));
  } else if (workload == "repeated_variables") {
    for (std::size_t i = 0; i < size; ++i) {
      auto a = graph.add(Node::leaf("x" + std::to_string(i)));
      root = graph.add(Node::node("pair", {a, a}));
      graph.add(Node::node("pair", {a, graph.add(Node::leaf("other"))}));
    }
    rules.push_back(eggc::rewrite<Node, Facts>("same", "(pair ?x ?x)", "?x"));
  } else if (workload == "rewrite_growth") {
    auto one = graph.add(Node::leaf("1"));
    root = one;
    for (std::size_t i = 0; i < size; ++i) {
      auto a = graph.add(Node::leaf("x" + std::to_string(i)));
      root = graph.add(
          Node::node("*", {one, graph.add(Node::node("+", {a, root}))}));
    }
    rules.push_back(
        eggc::rewrite<Node, Facts>("commute", "(+ ?x ?y)", "(+ ?y ?x)"));
    rules.push_back(eggc::rewrite<Node, Facts>("associate", "(+ ?x (+ ?y ?z))",
                                               "(+ (+ ?x ?y) ?z)"));
    rules.push_back(eggc::rewrite<Node, Facts>("distribute", "(* ?x (+ ?y ?z))",
                                               "(+ (* ?x ?y) (* ?x ?z))"));
    rules.push_back(eggc::rewrite<Node, Facts>("one", "(* 1 ?x)", "?x"));
  } else {
    throw std::invalid_argument("unknown workload");
  }

  *evaluations = 0;
  auto start = Clock::now();
  eggc::RunReport report;
  if (rules.empty())
    graph.rebuild();
  else
    report = eggc::run(graph, rules, options);
  auto elapsed = Clock::now() - start;
  start = Clock::now();
  auto [cost, best] = eggc::Extractor<Node, Facts>(graph).find_best(root);
  auto extraction = Clock::now() - start;
  if (best.nodes.empty()) throw std::logic_error("empty extracted expression");
  if (workload == "analysis_chain" && graph.analysis_data(root) != 1)
    throw std::logic_error("analysis did not reach the root");
  return {workload,
          size,
          graph.node_count(),
          graph.class_count(),
          *evaluations,
          std::move(report),
          elapsed,
          extraction,
          cost};
}

void print(const Sample& sample, std::size_t repeat) {
  std::size_t matches = 0, applications = 0;
  std::chrono::nanoseconds search{0}, apply{0}, rebuild{0};
  for (const auto& iteration : sample.report.history) {
    matches += iteration.matches;
    applications += iteration.applications;
    search += iteration.search_time;
    apply += iteration.apply_time;
    rebuild += iteration.rebuild_time;
  }
  std::cout << sample.workload << ',' << sample.input_size << ',' << repeat
            << ',' << sample.nodes << ',' << sample.classes << ','
            << sample.evaluations << ',' << sample.report.iterations << ','
            << matches << ',' << applications << ','
            << reason(sample.report.reason) << ',' << sample.elapsed.count()
            << ',' << search.count() << ',' << apply.count() << ','
            << rebuild.count() << ',' << sample.extraction.count() << ','
            << sample.cost << '\n';
}

std::size_t number(std::string_view text) {
  std::size_t result = 0;
  auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), result);
  if (error != std::errc{} || end != text.data() + text.size() || result == 0 ||
      result > 10000)
    throw std::invalid_argument(
        "size and repetitions must be between 1 and 10000");
  return result;
}
}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc > 4)
      throw std::invalid_argument(
          "usage: engine_benchmark [workload|all] [size] [repetitions]");
    std::string_view workload = argc > 1 ? argv[1] : "all";
    auto size = argc > 2 ? number(argv[2]) : 100;
    auto repetitions = argc > 3 ? number(argv[3]) : 3;
    // AST size overflows for sufficiently deep binary sharing. Keep this
    // workload small so extraction measures a finite supported cost.
    if (workload == "shared_expression" && size > 30)
      throw std::invalid_argument("shared_expression size must not exceed 30");
    std::cout
        << "workload,input_size,repeat,nodes,classes,analysis_evaluations,"
           "iterations,matches,applications,stop_reason,elapsed_ns,search_ns,"
           "apply_ns,rebuild_ns,extraction_ns,cost\n";
    for (auto name :
         {"analysis_chain", "congruence_cascade", "repeated_variables",
          "rewrite_growth", "shared_expression", "small_update"}) {
      if (workload != "all" && workload != name) continue;
      auto input_size = size;
      if (workload == "all" && std::string_view(name) == "shared_expression")
        input_size = std::min<std::size_t>(size, 30);
      for (std::size_t repeat = 0; repeat < repetitions; ++repeat)
        print(measure(name, input_size), repeat);
    }
    if (workload != "all" && workload != "analysis_chain" &&
        workload != "congruence_cascade" && workload != "repeated_variables" &&
        workload != "rewrite_growth" && workload != "shared_expression" &&
        workload != "small_update")
      throw std::invalid_argument("unknown workload");
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
