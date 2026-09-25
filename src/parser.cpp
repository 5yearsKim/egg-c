#include "eggc/parser.hpp"
#include <stdexcept>
#include <utility>

namespace eggc {
namespace {
Pattern pattern_from_expr(const RecExpr& expr, ExprId id) {
    const auto& node = expr.nodes.at(id.value);
    if (!node.op.empty() && node.op.front() == '?') {
        if (!node.children.empty())
            throw std::invalid_argument("pattern variable " + node.op + " cannot have children");
        return Pattern::var(node.op.substr(1));
    }
    std::vector<Pattern> children;
    children.reserve(node.children.size());
    for (const auto child : node.children) children.push_back(pattern_from_expr(expr, child));
    return Pattern::node(node.op, std::move(children));
}
}

Pattern parse_pattern(std::string_view text) {
    const auto expr = parse_expr(text);
    return pattern_from_expr(expr, expr.root());
}

Rewrite parse_rewrite(std::string name, std::string_view lhs, std::string_view rhs) {
    Rewrite result{std::move(name), parse_pattern(lhs), parse_pattern(rhs)};
    validate_rewrite(result);
    return result;
}

Rewrite parse_rewrite(std::string name, std::string_view lhs, std::string_view rhs,
                      Condition condition) {
    Rewrite result{std::move(name), parse_pattern(lhs), parse_pattern(rhs), std::move(condition)};
    validate_rewrite(result);
    return result;
}
}
