#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "eggc/all.hpp"

namespace example {
// A search filter has boolean literals, named fields, and logical operators.
struct MyFilterNode {
  enum class Kind { Boolean, Field, And, Or, Not };
  using Discriminant = Kind;
  Kind kind;
  bool value = false;
  std::string field;
  std::vector<eggc::Id> args;
  Kind discriminant() const { return kind; }
  const auto& children() const { return args; }
  auto& children_mut() { return args; }
  bool matches(const MyFilterNode& other) const {
    return kind == other.kind && value == other.value && field == other.field &&
           args.size() == other.args.size();
  }
  bool operator==(const MyFilterNode&) const = default;
  std::size_t hash() const {
    std::size_t seed = std::hash<bool>{}(value);
    eggc::hash_combine(seed, std::hash<std::string>{}(field));
    eggc::hash_combine(seed, static_cast<std::size_t>(kind));
    for (auto child : args) eggc::hash_combine(seed, child);
    return seed;
  }
};
static_assert(eggc::Language<MyFilterNode>);
}  // namespace example

// Parsing and printing are optional extensions to the node contract.
namespace eggc {
template <>
struct LanguageIO<example::MyFilterNode> {
  using Node = example::MyFilterNode;
  static Node from_op(std::string_view op, std::vector<Id> children) {
    if (op == "and" || op == "or" || op == "not") {
      const auto arity = op == "not" ? 1u : 2u;
      if (children.size() != arity)
        throw std::invalid_argument("wrong number of filter operands");
      const auto kind = op == "and"  ? Node::Kind::And
                        : op == "or" ? Node::Kind::Or
                                     : Node::Kind::Not;
      return {kind, false, {}, std::move(children)};
    }
    if (!children.empty())
      throw std::invalid_argument("unknown filter operator");
    if (op == "true" || op == "false")
      return {Node::Kind::Boolean, op == "true", {}, {}};
    return {Node::Kind::Field, false, std::string(op), {}};
  }
  static std::string format_op(const Node& node) {
    switch (node.kind) {
      case Node::Kind::Boolean:
        return node.value ? "true" : "false";
      case Node::Kind::Field:
        return node.field;
      case Node::Kind::And:
        return "and";
      case Node::Kind::Or:
        return "or";
      case Node::Kind::Not:
        return "not";
    }
    throw std::invalid_argument("unknown filter kind");
  }
};
}  // namespace eggc

int main() {
  try {
    using Node = example::MyFilterNode;
    // Optional UI settings can leave redundant true/false terms in a filter.
    auto input = eggc::parse_expr<Node>("(and true (or in_stock false))");
    std::vector rules{
        eggc::rewrite<Node>("and-true", "(and true ?x)", "?x"),
        eggc::rewrite<Node>("or-false", "(or ?x false)", "?x"),
        eggc::rewrite<Node>("double-not", "(not (not ?x))", "?x"),
    };
    eggc::EGraph<Node> graph;
    auto root = graph.add_expr(input);
    auto report = eggc::run(graph, rules);
    auto [cost, best] = eggc::Extractor<Node>(graph).find_best(root);
    if (report.reason != eggc::StopReason::Saturated || cost != 1 ||
        best.nodes.back().kind != Node::Kind::Field ||
        best.nodes.back().field != "in_stock")
      throw std::runtime_error("filter simplification failed");
    std::cout << eggc::to_string(input) << " -> " << eggc::to_string(best)
              << '\n';
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
