#include "eggc/printer.hpp"
#include "eggc/symbol_lang.hpp"

#include <iostream>
#include <stdexcept>

namespace test {
// A print-only language: no parser adapter is needed to inspect its graph.
struct Label : eggc::SymbolLang {};
struct Analysis {
  using Data = std::monostate;
  Data make(const eggc::EGraph<Label, Analysis> &, const Label &) const {
    return {};
  }
  eggc::AnalysisMerge merge(Data &, const Data &) const {
    return eggc::AnalysisMerge::Unchanged;
  }
};
} // namespace test
namespace eggc {
template <> struct LanguageIO<test::Label> {
  static std::string format_op(const test::Label &node) {
    return "custom:" + node.op;
  }
};
} // namespace eggc

namespace {
void check(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
template <class F> void requires_rebuild(F fn) {
  try {
    fn();
  } catch (const std::logic_error &) {
    return;
  }
  throw std::runtime_error("printed a dirty graph");
}
} // namespace

int main() {
  try {
    using Node = eggc::SymbolLang;
    check(eggc::to_string(Node::leaf("a")) == "a", "leaf printing");
    check(eggc::to_string(Node::node("+", {2, 3})) == "(+ e2 e3)",
          "node child order");
    check(eggc::to_string(Node::leaf("?literal")) == "\"?literal\"",
          "literal quoting");
    check(eggc::to_string(Node::node("op\n\"", {0})) == "(\"op\\n\\\"\" e0)",
          "operator escaping");

    eggc::EGraph<Node> graph;
    check(eggc::to_string(graph).empty(), "empty graph printing");
    auto a = graph.add(Node::leaf("a"));
    auto b = graph.add(Node::leaf("b"));
    auto f = graph.add(Node::node("f", {a, b}));
    graph.add(Node::leaf("unused"));
    requires_rebuild([&] { eggc::to_string(graph); });
    graph.rebuild();
    const auto revision = graph.revision();
    check(eggc::to_string(graph) ==
              "e0:\n  a\ne1:\n  b\ne2:\n  (f e0 e1)\ne3:\n  unused\n",
          "graph classes, nodes, and disconnected classes");
    check(graph.revision() == revision && graph.is_clean(),
          "printing mutated the graph");

    graph.merge(a, b);
    requires_rebuild([&] { eggc::to_string(graph); });
    graph.rebuild();
    const auto canonical = std::to_string(graph.find(a));
    const auto merged = eggc::to_string(graph);
    check(merged.find("(f e" + canonical + " e" + canonical + ")") !=
              std::string::npos,
          "canonical child references");
    check(merged.find("e" + std::to_string(b) + ":\n") == std::string::npos,
          "printed a merged-away class");

    graph.merge(a, f);
    graph.rebuild();
    const auto cycle_id = std::to_string(graph.find(f));
    check(eggc::to_string(graph).find("(f e" + cycle_id + " e" + cycle_id +
                                      ")") != std::string::npos,
          "cyclic graph printing");

    static_assert(eggc::PrintableLanguage<test::Label>);
    static_assert(!eggc::ParseableLanguage<test::Label>);
    eggc::EGraph<test::Label, test::Analysis> custom;
    auto value = custom.add(test::Label{Node::leaf("42")});
    custom.add(test::Label{Node::node("neg", {value})});
    custom.rebuild();
    check(eggc::to_string(custom) ==
              "e0:\n  custom:42\ne1:\n  (custom:neg e0)\n",
          "custom node formatter and analysis");
    std::cout << "Printer checks passed\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
