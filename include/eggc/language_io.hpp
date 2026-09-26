#pragma once
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "language.hpp"

namespace eggc {
// Optional adapter between operator text and a language node. The parser owns
// parentheses and variables; this trait owns operator names, literals, and
// arity. from_op preserves children in order and throws std::invalid_argument
// for an unsupported token or arity. format_op returns the operator/literal
// alone.
template <class L>
struct LanguageIO;

template <class L>
concept ParseableLanguage =
    Language<L> && requires(std::string_view token, std::vector<Id> children) {
      { LanguageIO<L>::from_op(token, std::move(children)) } -> std::same_as<L>;
    };

template <class L>
concept PrintableLanguage = Language<L> && requires(const L& node) {
  { LanguageIO<L>::format_op(node) } -> std::same_as<std::string>;
};
}  // namespace eggc
