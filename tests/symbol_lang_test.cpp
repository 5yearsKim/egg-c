#include "eggc/symbol_lang.hpp"

#include <iostream>
#include <stdexcept>

#include "eggc/egraph.hpp"

namespace {
void check(bool condition) {
  if (!condition)
    throw std::runtime_error("symbol language check failed");
}
static_assert(eggc::Language<eggc::SymbolLang>);
static_assert(eggc::ParseableLanguage<eggc::SymbolLang>);
static_assert(eggc::PrintableLanguage<eggc::SymbolLang>);
} // namespace
int main() {
  try {
    using Node = eggc::SymbolLang;
    auto left = Node::node("+", {1, 2});
    auto same = Node::node("+", {1, 2});
    auto reversed = Node::node("+", {2, 1});
    check(left == same && left.hash() == same.hash());
    check(left != reversed && left.matches(reversed));
    check(!left.matches(Node::node("+", {1})));
    check(!left.matches(Node::node("*", {1, 2})));

    eggc::EGraph<Node> graph;
    auto a = graph.add(Node::leaf("a"));
    auto b = graph.add(Node::leaf("b"));
    auto x = graph.add(Node::node("+", {a, b}));
    auto y = graph.add(Node::node("+", {b, a}));
    check(x == graph.add(Node::node("+", {a, b})));
    graph.merge(a, b);
    graph.rebuild();
    check(graph.find(x) == graph.find(y));
    std::cout << "SymbolLang checks passed\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
