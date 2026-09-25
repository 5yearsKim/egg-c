#pragma once
#include "pattern.hpp"

namespace eggc {
struct Rewrite { std::string name; Pattern lhs; Pattern rhs; };
enum class StopReason { Saturated, IterationLimit, NodeLimit };
struct RunReport { StopReason reason; std::size_t iterations; std::size_t nodes; };
RunReport run(EGraph& graph, const std::vector<Rewrite>& rules,
              std::size_t iteration_limit = 10, std::size_t node_limit = 10000);
}
