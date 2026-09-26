#include <charconv>
#include <iostream>
#include <stdexcept>

#include "eggc/all.hpp"

namespace example {
// This application language stores integer literals as integers, not strings.
struct MathNode {
  enum class Kind { Number, Add };
  using Discriminant = Kind;
  Kind kind;
  int value;
  std::vector<eggc::Id> args;
  Kind discriminant() const { return kind; }
  const auto& children() const { return args; }
  auto& children_mut() { return args; }
  bool matches(const MathNode& other) const {
    return kind == other.kind && value == other.value &&
           args.size() == other.args.size();
  }
  bool operator==(const MathNode&) const = default;
  std::size_t hash() const {
    std::size_t seed = std::hash<int>{}(value);
    eggc::hash_combine(seed, static_cast<std::size_t>(kind));
    for (auto child : args) eggc::hash_combine(seed, child);
    return seed;
  }
};
static_assert(eggc::Language<MathNode>);
}  // namespace example

// Parsing and printing are optional extensions to the node contract.
namespace eggc {
template <>
struct LanguageIO<example::MathNode> {
  using Node = example::MathNode;
  static Node from_op(std::string_view op, std::vector<Id> children) {
    if (op == "+") {
      if (children.size() != 2)
        throw std::invalid_argument("+ requires two operands");
      return {Node::Kind::Add, 0, std::move(children)};
    }
    if (!children.empty()) throw std::invalid_argument("unknown operator");
    int value;
    auto [end, error] =
        std::from_chars(op.data(), op.data() + op.size(), value);
    if (error != std::errc{} || end != op.data() + op.size())
      throw std::invalid_argument("expected an integer literal");
    return {Node::Kind::Number, value, {}};
  }
  static std::string format_op(const Node& node) {
    return node.kind == Node::Kind::Add ? "+" : std::to_string(node.value);
  }
};
}  // namespace eggc

int main() {
  try {
    using Node = example::MathNode;
    auto input = eggc::parse_expr<Node>("(+ 7 0)");
    auto rule = eggc::rewrite<Node>("add-zero", "(+ ?x 0)", "?x");
    eggc::EGraph<Node> graph;
    auto root = graph.add_expr(input);
    auto report = eggc::run(graph, std::vector{rule});
    auto [cost, best] = eggc::Extractor<Node>(graph).find_best(root);
    if (report.reason != eggc::StopReason::Saturated || cost != 1 ||
        best.nodes.back().kind != Node::Kind::Number ||
        best.nodes.back().value != 7)
      throw std::runtime_error("custom-language simplification failed");
    std::cout << eggc::to_string(input) << " -> " << eggc::to_string(best)
              << '\n';
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
