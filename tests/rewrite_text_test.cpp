#include <charconv>
#include <iostream>
#include <stdexcept>

#include "eggc/all.hpp"

namespace {
using Node = eggc::SymbolLang;
struct Values {
  using Data = std::optional<int>;
  Data make(const eggc::EGraph<Node, Values>&, const Node& node) const {
    if (!node.args.empty()) return {};
    int value;
    auto [end, error] =
        std::from_chars(node.op.data(), node.op.data() + node.op.size(), value);
    if (error == std::errc{} && end == node.op.data() + node.op.size())
      return value;
    return {};
  }
  eggc::AnalysisMerge merge(Data& into, const Data& from) const {
    if (!from || into == from) return eggc::AnalysisMerge::Unchanged;
    if (into) return eggc::AnalysisMerge::Conflict;
    into = from;
    return eggc::AnalysisMerge::Changed;
  }
};
using Graph = eggc::EGraph<Node, Values>;
void check(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
template <class F>
void fails(F fn, std::string_view fragment) {
  try {
    fn();
  } catch (const std::invalid_argument& error) {
    check(
        std::string_view(error.what()).find(fragment) != std::string_view::npos,
        "missing error context");
    return;
  }
  throw std::runtime_error("expected rewrite validation error");
}
}  // namespace
int main() {
  try {
    const auto parsed = eggc::rewrite("add-zero", "(+ ?x 0)", "?x");
    using Pat = eggc::Pattern<Node>;
    eggc::Rewrite<Node> built{
        "add-zero",
        Pat::node(Node::node("+", {0, 0}),
                  {Pat::var("x"), Pat::node(Node::leaf("0"))}),
        Pat::var("x")};
    for (const auto& rule : {parsed, built}) {
      eggc::EGraph<Node> graph;
      auto root = graph.add_expr(eggc::parse_expr("(+ a 0)"));
      check(eggc::run(graph, std::vector{rule}).reason ==
                eggc::StopReason::Saturated,
            "saturation");
      auto [cost, best] = eggc::Extractor<Node>(graph).find_best(root);
      check(cost == 1 && eggc::to_string(best) == "a",
            "parsed/direct equivalence");
    }
    fails([] { eggc::rewrite("bad-lhs", "(+ ?x", "?x"); }, "bad-lhs\", LHS");
    fails([] { eggc::rewrite("bad-rhs", "?x", "("); }, "bad-rhs\", RHS");
    fails([] { eggc::rewrite("unbound", "?x", "?y"); }, "unbound\", RHS");
    bool caught = false;
    try {
      eggc::rewrite("location", "?x", "\n (");
    } catch (const eggc::ParseError& error) {
      caught = true;
      check(error.line() == 2 && error.column() == 3,
            "rewrite source location");
    }
    check(caught, "missing rewrite parse failure");

    bool saw_clean = false;
    eggc::Condition<Node, Values> nonzero{
        "nonzero",
        {"?x"},
        [&](const Graph& graph, eggc::Id, const eggc::Substitution& subst) {
          saw_clean = graph.is_clean();
          const auto& value = graph.analysis_data(subst.at("?x"));
          return value && *value != 0;
        }};
    // Infer both Node and Values from the condition, or specify them
    // explicitly.
    auto guarded = eggc::rewrite("div-self", "(/ ?x ?x)", "1", nonzero);
    auto explicit_rule =
        eggc::rewrite<Node, Values>("div-self", "(/ ?x ?x)", "1", nonzero);
    static_assert(std::same_as<decltype(guarded), eggc::Rewrite<Node, Values>>);
    for (const auto* input : {"(/ 2 2)", "(/ 0 0)", "(/ a a)"}) {
      Graph graph;
      auto root = graph.add_expr(eggc::parse_expr(input));
      const auto initial_nodes = graph.node_count();
      auto report = eggc::run(graph, std::vector{guarded});
      auto [cost, best] = eggc::Extractor<Node, Values>(graph).find_best(root);
      const bool allowed = std::string_view(input) == "(/ 2 2)";
      check(eggc::to_string(best) == (allowed ? "1" : input),
            "condition result");
      check(saw_clean && report.history.front().condition_checks == 1,
            "condition accounting");
      check(report.history.front().condition_rejections == (allowed ? 0u : 1u),
            "rejection accounting");
      if (!allowed)
        check(cost == 3 && graph.node_count() == initial_nodes,
              "rejected rule mutated graph");
    }
    Graph graph;
    auto root = graph.add_expr(eggc::parse_expr("(/ a a)"));
    auto unknown = graph.add(Node::leaf("a"));
    eggc::run(graph, std::vector{explicit_rule});
    graph.merge(unknown,
                graph.add(Node::leaf("2")));  // Supply a new proven fact.
    eggc::run(graph, std::vector{guarded});
    check(
        eggc::to_string(
            eggc::Extractor<Node, Values>(graph).find_best(root).second) == "1",
        "new analysis fact");
    auto bad = nonzero;
    bad.required_variables = {"?missing"};
    fails([&] { eggc::rewrite("bad-condition", "?x", "?x", bad); },
          "bad-condition\", condition");
    bad = nonzero;
    bad.check = {};
    fails([&] { eggc::rewrite("empty-condition", "?x", "?x", bad); },
          "empty rewrite condition");
    Graph limited;
    auto unchanged = limited.add_expr(eggc::parse_expr("(/ 0 0)"));
    auto count = limited.node_count();
    eggc::RunOptions options;
    options.match_limit = 0;
    check(eggc::run(limited, std::vector{guarded}, options).reason ==
              eggc::StopReason::MatchLimit,
          "rejected match budget");
    check(limited.node_count() == count && limited.find(unchanged) == unchanged,
          "limited run mutation");
    std::cout << "Text rewrite and condition checks passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
