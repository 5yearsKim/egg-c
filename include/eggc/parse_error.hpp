#pragma once
#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace eggc {
// Source positions use a zero-based byte offset and one-based line/column.
class ParseError : public std::invalid_argument {
public:
  ParseError(std::string_view input, std::size_t offset, std::string message)
      : std::invalid_argument(describe(input, offset, message)),
        offset_(std::min(offset, input.size())), message_(std::move(message)) {
    const auto [line, column] = position(input, offset_);
    line_ = line;
    column_ = column;
  }
  std::size_t offset() const noexcept { return offset_; }
  std::size_t line() const noexcept { return line_; }
  std::size_t column() const noexcept { return column_; }
  const std::string &message() const noexcept { return message_; }

private:
  static std::pair<std::size_t, std::size_t> position(std::string_view input,
                                                      std::size_t offset) {
    std::size_t line = 1, column = 1;
    for (std::size_t i = 0; i < std::min(offset, input.size()); ++i) {
      if (input[i] == '\n') {
        ++line;
        column = 1;
      } else {
        ++column;
      }
    }
    return {line, column};
  }
  static std::string describe(std::string_view input, std::size_t offset,
                              const std::string &message) {
    const auto [line, column] = position(input, offset);
    return "line " + std::to_string(line) + ", column " +
           std::to_string(column) + ": " + message;
  }
  std::size_t offset_, line_ = 1, column_ = 1;
  std::string message_;
};
} // namespace eggc
