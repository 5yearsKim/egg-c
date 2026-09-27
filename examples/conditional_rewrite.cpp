#include <charconv>
#include <iostream>
#include <stdexcept>

#include "eggc/all.hpp"

namespace {
using Node = eggc::SymbolLang;
using Graph = eggc::EGraph<Node>;

// Allow the rewrite only if ?x has a known nonzero integer literal.
bool is_nonzero(const Graph& graph, eggc::Id /* matched_class */,
                const eggc::Substitution& bindings) {
  for (const auto& node : graph.nodes(bindings.at("?x"))) {
    if (!node.args.empty()) continue;  // Skip expressions such as (+ 1 1).

    int value;
    auto [end, error] =
        std::from_chars(node.op.data(), node.op.data() + node.op.size(), value);
    if (error != std::errc{} || end != node.op.data() + node.op.size()) {
      continue;  // Symbols such as a are not known integers.
    }
    if (value != 0) return true;
  }
  return false;
}
}  // namespace

int main() {
  try {
    eggc::Condition<Node> nonzero{"nonzero", {"?x"}, is_nonzero};
    auto rule = eggc::rewrite("div-self", "(/ ?x ?x)", "1", nonzero);
    for (const auto* input : {"(/ 2 2)", "(/ 0 0)", "(/ a a)"}) {
      Graph graph;
      auto root = graph.add_expr(eggc::parse_expr(input));
      auto report = eggc::run(graph, std::vector{rule});
      auto [cost, best] = eggc::Extractor<Node>(graph).find_best(root);
      const bool allowed = std::string_view(input) == "(/ 2 2)";
      if (report.reason != eggc::StopReason::Saturated ||
          eggc::to_string(best) != (allowed ? "1" : input) ||
          cost != (allowed ? 1u : 3u))
        throw std::runtime_error("conditional rewrite failed");
      std::cout << input << " -> " << eggc::to_string(best) << '\n';
    }
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
