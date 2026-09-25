#include "eggc/rewrite.hpp"
#include <set>
#include <stdexcept>

namespace eggc {
namespace {
void collect_variables(const Pattern& pattern, std::set<std::string>& variables,
                       const std::string& rule_name, const char* side) {
    if (pattern.is_var()) {
        if (!pattern.children.empty())
            throw std::invalid_argument("rewrite '" + rule_name + "': variable " + pattern.op +
                                        " has children in " + side);
        variables.insert(pattern.op);
        return;
    }
    for (const auto& child : pattern.children)
        collect_variables(child, variables, rule_name, side);
}
}

void validate_rewrite(const Rewrite& rule) {
    std::set<std::string> lhs_variables;
    std::set<std::string> rhs_variables;
    collect_variables(rule.lhs, lhs_variables, rule.name, "lhs");
    collect_variables(rule.rhs, rhs_variables, rule.name, "rhs");
    for (const auto& variable : rhs_variables)
        if (!lhs_variables.count(variable))
            throw std::invalid_argument("rewrite '" + rule.name +
                                        "' has unbound rhs variable " + variable);
}
}
