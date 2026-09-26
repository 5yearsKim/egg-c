#include <charconv>
#include <limits>
#include <memory>

#include "support/check.hpp"

namespace {
using namespace test_support;
struct Values {
  using Data = std::optional<int>;
  Data make(const eggc::EGraph<Node, Values>& graph, const Node& node) const {
    if (node.args.empty()) {
      int value = 0;
      const auto [end, error] = std::from_chars(
          node.op.data(), node.op.data() + node.op.size(), value);
      if (error == std::errc{} && end == node.op.data() + node.op.size())
        return value;
    }
    if (node.op == "+" && node.args.size() == 2) {
      auto a = graph.analysis_data(node.args[0]),
           b = graph.analysis_data(node.args[1]);
      if (a && b) {
        const long long sum = static_cast<long long>(*a) + *b;
        if (sum >= std::numeric_limits<int>::min() &&
            sum <= std::numeric_limits<int>::max())
          return static_cast<int>(sum);
      }
    }
    return {};
  }
  eggc::AnalysisMerge merge(Data& into, const Data& from) const {
    if (!from || into == from) return eggc::AnalysisMerge::Unchanged;
    if (into) return eggc::AnalysisMerge::Conflict;
    into = from;
    return eggc::AnalysisMerge::Changed;
  }
  void modify(eggc::EGraph<Node, Values>& graph, eggc::Id id) {
    const auto value =
        graph.analysis_data(id);  // Copy across add() reallocation.
    if (value) graph.merge(id, graph.add(Node::leaf(std::to_string(*value))));
  }
};
struct Growing {
  using Data = std::monostate;
  Data make(const eggc::EGraph<Node, Growing>&, const Node&) const {
    return {};
  }
  eggc::AnalysisMerge merge(Data&, const Data&) const {
    return eggc::AnalysisMerge::Unchanged;
  }
  void modify(eggc::EGraph<Node, Growing>& graph, eggc::Id id) {
    graph.add(Node::node("f", {id}));
  }
};
struct CountFacts {
  using Data = bool;
  std::shared_ptr<std::size_t> calls;
  Data make(const eggc::EGraph<Node, CountFacts>& g, const Node& node) const {
    ++*calls;
    if (node.op == "known") return true;
    for (auto child : node.args)
      if (g.analysis_data(child)) return true;
    return false;
  }
  eggc::AnalysisMerge merge(Data& into, const Data& from) const {
    if (into || !from) return eggc::AnalysisMerge::Unchanged;
    into = true;
    return eggc::AnalysisMerge::Changed;
  }
};
struct RetryingHook {
  using Data = std::monostate;
  std::shared_ptr<bool> fail;
  Data make(const eggc::EGraph<Node, RetryingHook>&, const Node&) const {
    return {};
  }
  eggc::AnalysisMerge merge(Data&, const Data&) const {
    return eggc::AnalysisMerge::Unchanged;
  }
  void modify(eggc::EGraph<Node, RetryingHook>& graph, eggc::Id id) {
    if (*fail) {
      *fail = false;
      throw std::runtime_error("transient hook failure");
    }
    graph.merge(id, graph.add(Node::leaf("done")));
  }
};
struct RecursiveHook {
  using Data = std::monostate;
  Data make(const eggc::EGraph<Node, RecursiveHook>&, const Node&) const {
    return {};
  }
  eggc::AnalysisMerge merge(Data&, const Data&) const {
    return eggc::AnalysisMerge::Unchanged;
  }
  void modify(eggc::EGraph<Node, RecursiveHook>& graph, eggc::Id) {
    graph.rebuild();
  }
};
void hook_exceptions() {
  eggc::EGraph<Node, RetryingHook> retry(
      RetryingHook{std::make_shared<bool>(true)});
  retry.add(Node::leaf("start"));
  throws<std::runtime_error>([&] { retry.rebuild(); });
  check(!retry.is_clean(), "throwing hook left graph marked clean");
  retry.rebuild();
  retry.check_invariants();
  check(retry.lookup(Node::leaf("done")).has_value(),
        "failed hook work was lost");
  eggc::EGraph<Node, RecursiveHook> recursive;
  recursive.add(Node::leaf("start"));
  throws<std::logic_error>([&] { recursive.rebuild(); });
}
void incremental_components() {
  auto calls = std::make_shared<std::size_t>(0);
  eggc::EGraph<Node, CountFacts> graph(CountFacts{calls});
  for (int i = 0; i < 2000; ++i) {
    auto id = graph.add(Node::leaf("unused" + std::to_string(i)));
    graph.add(Node::node("unused-parent", {id}));
  }
  auto x = graph.add(Node::leaf("x"));
  auto parent = graph.add(Node::node("f", {x, x}));
  graph.rebuild();
  graph.check_invariants();
  *calls = 0;
  graph.merge(x, graph.add(Node::leaf("known")));
  const auto before = *calls;
  graph.rebuild();
  check(graph.analysis_data(parent), "isolated update did not propagate");
  check(*calls - before <= 2,
        "isolated update scanned unrelated analysis nodes");
  check(graph.last_rebuild_stats().repaired_nodes <= 3,
        "isolated update repaired unrelated nodes");
  check(graph.last_rebuild_stats().refreshed_classes <= 3,
        "isolated update refreshed unrelated views");
  graph.check_invariants();
}
void analysis_hooks_and_limits() {
  eggc::EGraph<Node, Values> graph;
  graph.enable_explanations();
  auto root = graph.add_expr(eggc::parse_expr("(+ (+ 2 3) 4)"));
  graph.rebuild();
  graph.check_invariants();
  auto [cost, expr] = eggc::Extractor<Node, Values>(graph).find_best(root);
  check(cost == 1 && eggc::to_string(expr) == "9",
        "analysis hook did not fold nested constants");
  auto nine = graph.lookup(Node::leaf("9"));
  check(nine && graph.find(root) == *nine,
        "constant hook did not insert literal");
  const auto revision = graph.revision();
  graph.rebuild();
  check(graph.revision() == revision &&
            graph.last_rebuild_stats().modifications == 0,
        "idempotent analysis hook did not terminate");

  eggc::EGraph<Node, Growing> growing;
  auto base = growing.add(Node::leaf("base"));
  eggc::RunOptions options;
  options.node_limit = 12;
  auto stopped =
      eggc::run(growing, std::vector<eggc::Rewrite<Node, Growing>>{}, options);
  check(stopped.reason == eggc::StopReason::NodeLimit &&
            growing.node_count() == 12 && growing.is_clean(),
        "expanding analysis hook ignored node budget");
  growing.check_invariants();
  check(eggc::Extractor<Node, Growing>(growing).best_cost(base) == 1,
        "budgeted hook rebuild left graph unqueryable");
  options.node_limit = 15;
  auto resumed =
      eggc::run(growing, std::vector<eggc::Rewrite<Node, Growing>>{}, options);
  check(resumed.reason == eggc::StopReason::NodeLimit &&
            growing.node_count() == 15,
        "suspended hooks did not resume");
  options.node_limit = 100;
  options.time_limit = std::chrono::milliseconds(0);
  auto timed =
      eggc::run(growing, std::vector<eggc::Rewrite<Node, Growing>>{}, options);
  check(
      timed.reason == eggc::StopReason::TimeLimit && growing.node_count() == 15,
      "zero time budget executed pending hook");
}

}  // namespace
int main() {
  return test_support::run_tests(hook_exceptions, incremental_components,
                                 analysis_hooks_and_limits);
}
