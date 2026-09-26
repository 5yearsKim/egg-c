#pragma once
#include <string>

#include "egraph.hpp"
#include "expr.hpp"
#include "language_io.hpp"
#include "pattern.hpp"

namespace eggc {
// Node children are printed as stored IDs (eN), without expanding them.
template <PrintableLanguage L> std::string to_string(const L &node);
// Print every canonical class and its nodes, using canonical child IDs.
// Requires a clean graph; an empty graph produces an empty string.
template <PrintableLanguage L, class A>
std::string to_string(const EGraph<L, A> &graph);
template <PrintableLanguage L> std::string to_string(const RecExpr<L> &expr);
template <PrintableLanguage L> std::string to_string(const Pattern<L> &pattern);
} // namespace eggc
#include "impl/printer.tpp"
