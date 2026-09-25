#pragma once
#include "id.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace eggc {
struct ExprId {
    std::uint32_t value;
    bool operator==(const ExprId& rhs) const noexcept { return value == rhs.value; }
};

struct ExprNode {
    std::string op;
    std::vector<ExprId> children;
};

// A tree view of an expression, convenient for callers that need nested nodes.
struct Expr { std::string op; std::vector<Expr> children; };

// A bottom-up expression DAG: every child must refer to an earlier node.
struct RecExpr {
    std::vector<ExprNode> nodes;
    ExprId root() const;
};

RecExpr parse_expr(std::string_view text);
std::string to_string(const RecExpr& expr);
std::string to_string(const Expr& expr);
}
