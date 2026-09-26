#pragma once
#include <string>
#include <utility>
#include <vector>

#include "language_io.hpp"

namespace eggc {
// A ready-to-use language. Operators and literals are symbols; rewrites or
// application analysis supply their meaning.
struct SymbolLang {
  static constexpr bool exact_matches = true;
  std::string op;
  std::vector<Id> args;

  using Discriminant = std::string;
  Discriminant discriminant() const { return op; }
  const auto &children() const { return args; }
  auto &children_mut() { return args; }
  bool matches(const SymbolLang &other) const {
    return op == other.op && args.size() == other.args.size();
  }
  bool operator==(const SymbolLang &) const = default;
  std::size_t hash() const {
    auto seed = std::hash<std::string>{}(op);
    for (Id child : args)
      hash_combine(seed, child);
    return seed;
  }
  static SymbolLang leaf(std::string op) { return {std::move(op), {}}; }
  static SymbolLang node(std::string op, std::vector<Id> children) {
    return {std::move(op), std::move(children)};
  }
};
static_assert(Language<SymbolLang>);

template <> struct LanguageIO<SymbolLang> {
  static SymbolLang from_op(std::string_view token, std::vector<Id> children) {
    return SymbolLang::node(std::string(token), std::move(children));
  }
  static std::string format_op(const SymbolLang &node) { return node.op; }
};
} // namespace eggc
