// Included by parser.hpp.
#pragma once
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

#include "sexpr.hpp"

namespace eggc {
namespace text_detail {
// Shared iterative parser: no recursive calls or intermediate tree nodes.
template <ParseableLanguage L, bool IsPattern>
auto parse(std::string_view input) {
  using Entry = std::conditional_t<IsPattern, typename Pattern<L>::Entry, L>;
  std::vector<Entry> nodes;
  struct Frame {
    Token op;
    std::vector<Id> children;
  };
  std::vector<Frame> stack;
  std::optional<Id> root;
  Lexer lexer(input);
  const auto emit = [&](const Token& op, std::vector<Id> children) -> Id {
    if (nodes.size() >= invalid_id)
      throw ParseError(input, op.offset, "expression too large");
    const Id id = static_cast<Id>(nodes.size());
    if constexpr (IsPattern) {
      if (!op.quoted && op.text.starts_with('?')) {
        if (op.text.size() == 1)
          throw ParseError(input, op.offset, "empty pattern variable");
        nodes.emplace_back(Var{op.text});
        return id;
      }
    }
    try {
      L node = LanguageIO<L>::from_op(op.text, std::move(children));
      for (Id child : node.children())
        if (child >= id)
          throw std::invalid_argument("language returned an invalid child ID");
      nodes.emplace_back(std::move(node));
    } catch (const std::invalid_argument& error) {
      throw ParseError(input, op.offset, error.what());
    }
    return id;
  };
  const auto attach = [&](Id id) {
    if (stack.empty())
      root = id;
    else
      stack.back().children.push_back(id);
  };
  for (;;) {
    auto token = lexer.next();
    if (token.kind == TokenKind::End) {
      if (!stack.empty()) throw ParseError(input, token.offset, "expected ')'");
      if (!root)
        throw ParseError(input, token.offset, "expected an expression");
      return nodes;
    }
    if (root && stack.empty())
      throw ParseError(input, token.offset, "unexpected text after expression");
    switch (token.kind) {
      case TokenKind::Open: {
        auto op = lexer.next();
        if (op.kind != TokenKind::Atom)
          throw ParseError(input, op.offset, "expected an operator after '('");
        if constexpr (IsPattern) {
          if (!op.quoted && op.text.starts_with('?'))
            throw ParseError(input, op.offset,
                             "a pattern variable cannot be an operator");
        }
        stack.push_back({std::move(op), {}});
        break;
      }
      case TokenKind::Close: {
        if (stack.empty())
          throw ParseError(input, token.offset, "unexpected ')'");
        auto frame = std::move(stack.back());
        stack.pop_back();
        attach(emit(frame.op, std::move(frame.children)));
        break;
      }
      case TokenKind::Atom:
        attach(emit(token, {}));
        break;
      case TokenKind::End:
        break;
    }
  }
}

}  // namespace text_detail

template <ParseableLanguage L>
RecExpr<L> parse_expr(std::string_view text) {
  return {text_detail::parse<L, false>(text)};
}
template <ParseableLanguage L>
Pattern<L> parse_pattern(std::string_view text) {
  return {text_detail::parse<L, true>(text)};
}
}  // namespace eggc
