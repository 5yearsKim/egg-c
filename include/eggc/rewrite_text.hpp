#pragma once
#include <algorithm>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "parser.hpp"
#include "rewrite.hpp"

namespace eggc {
// String-based rule construction. The engine's Rewrite type is in rewrite.hpp.
namespace rewrite_text_detail {
template <ParseableLanguage L>
Pattern<L> rule_pattern(const std::string &name, std::string_view side,
                        std::string_view text) {
  try {
    return parse_pattern<L>(text);
  } catch (const ParseError &error) {
    // Retain the structured source location, adding the rule and side context.
    throw ParseError(text, error.offset(),
                     "rule \"" + name + "\", " + std::string(side) + ": " +
                         error.message());
  }
}
} // namespace rewrite_text_detail

// Parse and validate once, before the rule is used by a runner.
template <ParseableLanguage L = SymbolLang, class A = NoAnalysis<L>>
  requires AnalysisFor<A, L>
Rewrite<L, A> rewrite(std::string name, std::string_view lhs,
                      std::string_view rhs,
                      std::optional<Condition<L, A>> condition = {}) {
  auto left = rewrite_text_detail::rule_pattern<L>(name, "LHS", lhs);
  auto right = rewrite_text_detail::rule_pattern<L>(name, "RHS", rhs);
  Rewrite<L, A> rule{name, std::move(left), std::move(right),
                     std::move(condition)};
  try {
    validate_rewrite(rule);
  } catch (const std::invalid_argument &error) {
    std::string side = "condition";
    const auto bound = rule.lhs->variables();
    for (const auto &variable : rule.rhs->variables())
      if (std::find(bound.begin(), bound.end(), variable) == bound.end())
        side = "RHS";
    throw std::invalid_argument("rule \"" + name + "\", " + side + ": " +
                                error.what());
  }
  return rule;
}

// This overload permits inference of the language and analysis from a
// condition.
template <ParseableLanguage L, class A>
  requires AnalysisFor<A, L>
Rewrite<L, A> rewrite(std::string name, std::string_view lhs,
                      std::string_view rhs, Condition<L, A> condition) {
  return rewrite<L, A>(std::move(name), lhs, rhs,
                       std::optional<Condition<L, A>>{std::move(condition)});
}
} // namespace eggc
