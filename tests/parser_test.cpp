#include "eggc/parser.hpp"

#include <array>
#include <charconv>
#include <iostream>
#include <span>
#include <stdexcept>

namespace test {
// A typed language with fixed storage, proving the parser does not depend on
// SymbolLang's fields, strings, dynamic child storage, or default construction.
struct Calc {
  enum class Kind { Number, Add };
  using Discriminant = Kind;
  Kind kind;
  int value;
  std::array<eggc::Id, 2> operands{};
  explicit Calc(int n) : kind(Kind::Number), value(n) {}
  Calc(eggc::Id a, eggc::Id b) : kind(Kind::Add), value(0), operands{a, b} {}
  Kind discriminant() const { return kind; }
  std::span<const eggc::Id> children() const {
    return {operands.data(), kind == Kind::Add ? 2u : 0u};
  }
  std::span<eggc::Id> children_mut() {
    return {operands.data(), kind == Kind::Add ? 2u : 0u};
  }
  bool matches(const Calc &other) const {
    return kind == other.kind && value == other.value;
  }
  bool operator==(const Calc &) const = default;
  std::size_t hash() const {
    std::size_t h = std::hash<int>{}(value);
    eggc::hash_combine(h, static_cast<std::size_t>(kind));
    for (auto child : children())
      eggc::hash_combine(h, child);
    return h;
  }
};
struct NoText : eggc::SymbolLang {};
struct ParseOnly : eggc::SymbolLang {};
struct PrintOnly : eggc::SymbolLang {};
} // namespace test
namespace eggc {
template <> struct LanguageIO<test::Calc> {
  static test::Calc from_op(std::string_view op, std::vector<Id> children) {
    if (op == "+") {
      if (children.size() != 2)
        throw std::invalid_argument("+ requires two operands");
      return test::Calc(children[0], children[1]);
    }
    if (!children.empty())
      throw std::invalid_argument("unknown operator");
    int value;
    auto [end, error] =
        std::from_chars(op.data(), op.data() + op.size(), value);
    if (error != std::errc{} || end != op.data() + op.size())
      throw std::invalid_argument("invalid integer literal");
    return test::Calc(value);
  }
  static std::string format_op(const test::Calc &node) {
    return node.kind == test::Calc::Kind::Add ? "+"
                                              : std::to_string(node.value);
  }
};
template <> struct LanguageIO<test::ParseOnly> {
  static test::ParseOnly from_op(std::string_view op,
                                 std::vector<Id> children) {
    return {SymbolLang::node(std::string(op), std::move(children))};
  }
};
template <> struct LanguageIO<test::PrintOnly> {
  static std::string format_op(const test::PrintOnly &node) { return node.op; }
};
} // namespace eggc
namespace {
void check(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
template <class F> eggc::ParseError fails(F fn) {
  try {
    fn();
  } catch (const eggc::ParseError &error) {
    return error;
  }
  throw std::runtime_error("expected ParseError");
}
static_assert(eggc::Language<test::Calc>);
static_assert(!std::default_initializable<test::Calc>);
static_assert(eggc::Language<test::NoText> &&
              !eggc::ParseableLanguage<test::NoText>);
static_assert(!eggc::PrintableLanguage<test::NoText>);
static_assert(eggc::ParseableLanguage<test::ParseOnly> &&
              !eggc::PrintableLanguage<test::ParseOnly>);
static_assert(!eggc::ParseableLanguage<test::PrintOnly> &&
              eggc::PrintableLanguage<test::PrintOnly>);
} // namespace
int main() {
  try {
    for (const std::string text :
         {"a", "(+ a (* b -42))", "(list a b c)", "\"\"", "\"?literal\"",
          "(\"op name\" \"a(b)\" \"x\\\"y\\\\z\\n\\r\\t\")"}) {
      auto expr = eggc::parse_expr(text);
      auto printed = eggc::to_string(expr);
      check(eggc::parse_expr(printed).nodes == expr.nodes,
            "expression round trip failed");
      check(printed == text, "unexpected canonical output");
    }
    check(eggc::to_string(eggc::parse_expr(" \n (+\ta (b)) ")) == "(+ a b)",
          "whitespace normalization");
    check(eggc::parse_expr("?literal").nodes.back().op == "?literal",
          "expression variable confusion");
    auto pattern = eggc::parse_pattern("(+ ?x (* ?x \"?literal\"))");
    check(pattern.variables() == std::vector<std::string>{"?x"},
          "variable recognition");
    check(eggc::to_string(pattern) == "(+ ?x (* ?x \"?literal\"))",
          "pattern printing");
    check(eggc::to_string(eggc::parse_pattern(eggc::to_string(pattern))) ==
              eggc::to_string(pattern),
          "pattern round trip");
    try {
      eggc::to_string(eggc::Pattern<eggc::SymbolLang>::var("two words"));
      throw std::runtime_error(
          "printed a variable name that cannot round-trip");
    } catch (const std::invalid_argument &) {
    }
    eggc::EGraph<test::NoText> no_text;
    no_text.add(test::NoText{eggc::SymbolLang::leaf("a")});
    no_text.rebuild();
    check(no_text.class_count() == 1, "language without text support");
    auto parse_only = eggc::parse_expr<test::ParseOnly>("a");
    check(parse_only.nodes.back().op == "a", "parse-only language");
    eggc::RecExpr<test::PrintOnly> print_only;
    print_only.add(test::PrintOnly{eggc::SymbolLang::leaf("a")});
    check(eggc::to_string(print_only) == "a", "print-only language");

    eggc::RecExpr<eggc::SymbolLang> shared;
    shared.add(eggc::SymbolLang::leaf("unused"));
    auto a = shared.add(eggc::SymbolLang::leaf("a"));
    shared.add(eggc::SymbolLang::node("foo", {a, a}));
    check(eggc::to_string(shared) == "(foo a a)", "DAG printing");
    eggc::EGraph<eggc::SymbolLang> round_trip;
    auto dag_root = round_trip.add_expr(shared);
    auto text_root =
        round_trip.add_expr(eggc::parse_expr(eggc::to_string(shared)));
    check(round_trip.find(dag_root) == round_trip.find(text_root),
          "DAG round-trip identity");
    eggc::EGraph<eggc::SymbolLang> graph;
    auto root = graph.add_expr(eggc::parse_expr("(f a b)"));
    graph.rebuild();
    auto repeated = eggc::parse_pattern("(f ?x ?x)");
    check(eggc::match(graph, repeated, root).empty(),
          "repeated variables before merge");
    graph.merge(graph.add(eggc::SymbolLang::leaf("a")),
                graph.add(eggc::SymbolLang::leaf("b")));
    graph.rebuild();
    check(eggc::match(graph, repeated, root).size() == 1,
          "repeated variables after merge");

    for (const std::string text :
         {"", " ", "()", "((a) b)", "(", "(+ a", ")", "a b", "(a))", "\"a",
          "\"a\\q\"", "\"a\\", "a\"b", "\\a"})
      fails([&] { eggc::parse_expr(text); });
    fails([] { eggc::parse_pattern("?"); });
    fails([] { eggc::parse_pattern("(?x a)"); });
    auto error = fails([] { eggc::parse_expr("a\n  b"); });
    check(error.offset() == 4 && error.line() == 2 && error.column() == 3,
          "source location");

    auto typed = eggc::parse_expr<test::Calc>("(+ -3 42)");
    check(typed.nodes[0].value == -3 && typed.nodes[1].value == 42,
          "typed literals");
    check(eggc::to_string(typed) == "(+ -3 42)", "typed printing");
    check(eggc::parse_expr<test::Calc>(eggc::to_string(typed)).nodes ==
              typed.nodes,
          "typed round trip");
    check(eggc::to_string(eggc::parse_pattern<test::Calc>("(+ ?x 0)")) ==
              "(+ ?x 0)",
          "typed pattern");
    for (const auto *text :
         {"(+ 1)", "(+ 1 2 3)", "(* 1 2)", "abc", "99999999999999999999"})
      fails([&] { eggc::parse_expr<test::Calc>(text); });
    auto typed_error = fails([] { eggc::parse_expr<test::Calc>("\n (+ 1)"); });
    check(typed_error.line() == 2 && typed_error.column() == 3,
          "operator error location");

    // Deep input proves parsing and printing do not rely on the call stack.
    std::string deep;
    for (int i = 0; i < 10000; ++i)
      deep += "(f ";
    deep += "a";
    deep.append(10000, ')');
    check(eggc::to_string(eggc::parse_expr(deep)) == deep, "deep nesting");
    eggc::RecExpr<eggc::SymbolLang> invalid{{{"f", {0}}}};
    try {
      eggc::to_string(invalid);
      throw std::runtime_error("accepted cyclic expression");
    } catch (const std::invalid_argument &) {
    }
    std::cout << "Parser checks passed\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
