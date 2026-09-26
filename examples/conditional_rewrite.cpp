#include <charconv>
#include <iostream>
#include <stdexcept>

#include "eggc/all.hpp"

namespace {
using Node = eggc::SymbolLang;
// Facts are either an unknown value or a proven integer value. Operator nodes
// start unknown; this example does not implement arithmetic constant folding.
struct Values {
  using Data = std::optional<int>;
  Data make(const eggc::EGraph<Node, Values> &, const Node &node) const {
    if (!node.args.empty())
      return {};
    int value;
    auto [end, error] =
        std::from_chars(node.op.data(), node.op.data() + node.op.size(), value);
    if (error == std::errc{} && end == node.op.data() + node.op.size())
      return value;
    return {};
  }
  eggc::AnalysisMerge merge(Data &into, const Data &from) const {
    if (!from || into == from)
      return eggc::AnalysisMerge::Unchanged;
    if (into)
      return eggc::AnalysisMerge::Conflict;
    into = from;
    return eggc::AnalysisMerge::Changed;
  }
};
using Graph = eggc::EGraph<Node, Values>;
} // namespace

int main() {
  try {
    eggc::Condition<Node, Values> nonzero{
        "nonzero",
        {"?x"},
        [](const Graph &graph, eggc::Id, const eggc::Substitution &subst) {
          const auto &value = graph.analysis_data(subst.at("?x"));
          return value && *value != 0;
        }};
    auto rule = eggc::rewrite("div-self", "(/ ?x ?x)", "1", nonzero);
    for (const auto *input : {"(/ 2 2)", "(/ 0 0)", "(/ a a)"}) {
      Graph graph;
      auto root = graph.add_expr(eggc::parse_expr(input));
      auto report = eggc::run(graph, std::vector{rule});
      auto [cost, best] = eggc::Extractor<Node, Values>(graph).find_best(root);
      const bool allowed = std::string_view(input) == "(/ 2 2)";
      if (report.reason != eggc::StopReason::Saturated ||
          eggc::to_string(best) != (allowed ? "1" : input) ||
          cost != (allowed ? 1u : 3u))
        throw std::runtime_error("conditional rewrite failed");
      std::cout << input << " -> " << eggc::to_string(best) << '\n';
    }
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
