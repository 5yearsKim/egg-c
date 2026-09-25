#pragma once
#include "expr.hpp"
#include "rewrite.hpp"
#include <string_view>

namespace eggc {
Pattern parse_pattern(std::string_view text);
Rewrite parse_rewrite(std::string name, std::string_view lhs, std::string_view rhs);
Rewrite parse_rewrite(std::string name, std::string_view lhs, std::string_view rhs,
                      Condition condition);
}
