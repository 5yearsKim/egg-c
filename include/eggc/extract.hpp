#pragma once
#include "egraph.hpp"

namespace eggc {
struct Expr { std::string op; std::vector<Expr> children; };
Expr extract(const EGraph& graph, Id root);
std::string to_string(const Expr& expr);
}
