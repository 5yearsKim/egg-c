#pragma once
#include <string>
#include <string_view>

#include "expr.hpp"
#include "language_io.hpp"
#include "parse_error.hpp"
#include "pattern.hpp"
#include "printer.hpp"
#include "symbol_lang.hpp"

namespace eggc {
// Printing remains available here through printer.hpp for compatibility.
template <ParseableLanguage L = SymbolLang>
RecExpr<L> parse_expr(std::string_view text);
template <ParseableLanguage L = SymbolLang>
Pattern<L> parse_pattern(std::string_view text);
}  // namespace eggc
#include "impl/parser.tpp"
