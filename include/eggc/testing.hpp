#pragma once
#include "egraph.hpp"

namespace eggc::testing {
// Slow full-scan rebuilding oracle for deterministic differential tests and
// benchmarks. It does not run e-class analyses.
void rebuild_full_scan(EGraph &graph);
} // namespace eggc::testing
