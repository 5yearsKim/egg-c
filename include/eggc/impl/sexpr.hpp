#pragma once
#include <cctype>
#include <string>
#include <string_view>
#include <utility>

#include "../parse_error.hpp"

// Shared syntax: tokenization, quoted symbols, and escaping.
namespace eggc::text_detail {
inline bool whitespace(char c) {
  return std::isspace(static_cast<unsigned char>(c)) != 0;
}
enum class TokenKind { Atom, Open, Close, End };
struct Token {
  TokenKind kind;
  std::string text;
  std::size_t offset;
  bool quoted = false;
};
class Lexer {
public:
  explicit Lexer(std::string_view input) : input_(input) {}
  Token next() {
    while (position_ < input_.size() && whitespace(input_[position_]))
      ++position_;
    const auto start = position_;
    if (position_ == input_.size())
      return {TokenKind::End, {}, start};
    char c = input_[position_++];
    if (c == '(')
      return {TokenKind::Open, {}, start};
    if (c == ')')
      return {TokenKind::Close, {}, start};
    if (c == '"') {
      std::string value;
      while (position_ < input_.size()) {
        c = input_[position_++];
        if (c == '"')
          return {TokenKind::Atom, std::move(value), start, true};
        if (c == '\\') {
          if (position_ == input_.size())
            throw ParseError(input_, position_ - 1, "unfinished escape");
          switch (input_[position_++]) {
          case 'n':
            c = '\n';
            break;
          case 'r':
            c = '\r';
            break;
          case 't':
            c = '\t';
            break;
          case '\\':
            c = '\\';
            break;
          case '"':
            c = '"';
            break;
          default:
            throw ParseError(input_, position_ - 2, "invalid escape");
          }
        }
        value.push_back(c);
      }
      throw ParseError(input_, start, "unterminated quoted symbol");
    }
    while (position_ < input_.size()) {
      c = input_[position_];
      if (whitespace(c) || c == '(' || c == ')')
        break;
      if (c == '"' || c == '\\')
        throw ParseError(input_, position_,
                         "quote or escape requires a quoted symbol");
      ++position_;
    }
    if (input_[start] == '\\')
      throw ParseError(input_, start, "escape requires a quoted symbol");
    return {TokenKind::Atom,
            std::string(input_.substr(start, position_ - start)), start};
  }

private:
  std::string_view input_;
  std::size_t position_ = 0;
};

inline std::string quote(std::string_view symbol) {
  bool needs_quotes = symbol.empty() || symbol.starts_with('?');
  for (char c : symbol)
    needs_quotes |=
        whitespace(c) || c == '(' || c == ')' || c == '"' || c == '\\';
  if (!needs_quotes)
    return std::string(symbol);
  std::string result = "\"";
  for (char c : symbol) {
    switch (c) {
    case '\n':
      result += "\\n";
      break;
    case '\r':
      result += "\\r";
      break;
    case '\t':
      result += "\\t";
      break;
    case '\\':
      result += "\\\\";
      break;
    case '"':
      result += "\\\"";
      break;
    default:
      result += c;
    }
  }
  return result + '"';
}

} // namespace eggc::text_detail
