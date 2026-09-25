#pragma once
#include "enode.hpp"
#include <any>
#include <stdexcept>

namespace eggc {
class EGraph;

enum class AnalysisMerge { Unchanged, Changed, Conflict };

class AnalysisConflict : public std::runtime_error {
public:
    explicit AnalysisConflict(const std::string& message) : std::runtime_error(message) {}
};

// Type-erased per-e-class facts. Merge must be associative, commutative, and
// idempotent. make() reads child facts; modify() may add justified equalities.
class EClassAnalysis {
public:
    virtual ~EClassAnalysis() = default;
    virtual std::any make(const EGraph& graph, const ENode& node) const = 0;
    virtual AnalysisMerge merge(std::any& into, const std::any& from) const = 0;
    virtual void modify(EGraph&, Id) const {}
};
}
