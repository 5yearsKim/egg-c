#pragma once
#include "pattern.hpp"
#include <string>

namespace eggc {
struct Rewrite { std::string name; Pattern lhs; Pattern rhs; };
void validate_rewrite(const Rewrite& rewrite);
}
