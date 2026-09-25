#pragma once
#include "analysis.hpp"
#include "rewrite.hpp"
#include <cstdint>

namespace eggc {
struct ConstantFact {
    enum class Kind { Unknown, Known, Conflict } kind = Kind::Unknown;
    std::int64_t value = 0;
};

// Folds exact mathematical integer expressions when all observed values fit
// int64_t. Operations whose result overflows are left unknown and not folded.
class ConstantAnalysis final : public EClassAnalysis {
public:
    std::any make(const EGraph& graph, const ENode& node) const override;
    AnalysisMerge merge(std::any& into, const std::any& from) const override;
    void modify(EGraph& graph, Id id) const override;
};

// Requires a ConstantAnalysis-backed graph. Unknown facts reject the match;
// a missing/incompatible analysis is a configuration error.
Condition known_nonzero(std::string variable);
}
