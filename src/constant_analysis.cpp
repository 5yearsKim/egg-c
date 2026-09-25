#include "eggc/constant_analysis.hpp"
#include "eggc/egraph.hpp"
#include <charconv>
#include <limits>
#include <string>

namespace eggc {
namespace {
ConstantFact unknown() { return {}; }
ConstantFact known(std::int64_t value) { return {ConstantFact::Kind::Known, value}; }

bool parse_integer(const std::string& text, std::int64_t& value) {
    if (text.empty()) return false;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}

bool checked_add(std::int64_t a, std::int64_t b, std::int64_t& result) {
    if ((b > 0 && a > std::numeric_limits<std::int64_t>::max() - b) ||
        (b < 0 && a < std::numeric_limits<std::int64_t>::min() - b)) return false;
    result = a + b;
    return true;
}

bool checked_multiply(std::int64_t a, std::int64_t b, std::int64_t& result) {
    using Limits = std::numeric_limits<std::int64_t>;
    if (a == 0 || b == 0) { result = 0; return true; }
    if ((a == -1 && b == Limits::min()) || (b == -1 && a == Limits::min())) return false;
    if (a > 0) {
        if ((b > 0 && a > Limits::max() / b) || (b < 0 && b < Limits::min() / a)) return false;
    } else {
        if ((b > 0 && a < Limits::min() / b) || (b < 0 && a < Limits::max() / b)) return false;
    }
    result = a * b;
    return true;
}

const ConstantFact& fact(const EGraph& graph, Id id) {
    return std::any_cast<const ConstantFact&>(graph.analysis_data(id));
}
}

std::any ConstantAnalysis::make(const EGraph& graph, const ENode& node) const {
    if (node.children.empty()) {
        std::int64_t value;
        if (parse_integer(node.op, value)) return known(value);
        return unknown();
    }
    if ((node.op != "+" && node.op != "*") || node.children.size() != 2) return unknown();
    const auto& left = fact(graph, node.children[0]);
    const auto& right = fact(graph, node.children[1]);
    if (left.kind != ConstantFact::Kind::Known || right.kind != ConstantFact::Kind::Known)
        return unknown();
    std::int64_t result;
    const bool fits = node.op == "+"
        ? checked_add(left.value, right.value, result)
        : checked_multiply(left.value, right.value, result);
    return fits ? std::any(known(result)) : std::any(unknown());
}

AnalysisMerge ConstantAnalysis::merge(std::any& into, const std::any& from) const {
    auto& target = std::any_cast<ConstantFact&>(into);
    const auto& source = std::any_cast<const ConstantFact&>(from);
    if (target.kind == ConstantFact::Kind::Conflict || source.kind == ConstantFact::Kind::Conflict)
        return AnalysisMerge::Conflict;
    if (source.kind == ConstantFact::Kind::Unknown) return AnalysisMerge::Unchanged;
    if (target.kind == ConstantFact::Kind::Unknown) {
        target = source;
        return AnalysisMerge::Changed;
    }
    return target.value == source.value ? AnalysisMerge::Unchanged : AnalysisMerge::Conflict;
}

void ConstantAnalysis::modify(EGraph& graph, Id id) const {
    const auto value = fact(graph, id);
    if (value.kind != ConstantFact::Kind::Known) return;
    const auto literal = graph.add(std::to_string(value.value));
    graph.merge(id, literal);
}
}
