#include "eggc/constant_analysis.hpp"
#include "eggc/egraph.hpp"
#include <charconv>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>

namespace eggc {
namespace {
ConstantFact unknown() { return {}; }
ConstantFact known(std::int64_t value) {
  return {ConstantFact::Kind::Known, value};
}

std::optional<std::int64_t> parse_integer(const std::string &text) {
  if (text.empty())
    return std::nullopt;
  std::int64_t value;
  const auto parsed =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
    return std::nullopt;
  return value;
}

std::optional<std::int64_t> checked_add(std::int64_t a, std::int64_t b) {
  if ((b > 0 && a > std::numeric_limits<std::int64_t>::max() - b) ||
      (b < 0 && a < std::numeric_limits<std::int64_t>::min() - b))
    return std::nullopt;
  return a + b;
}

std::optional<std::int64_t> checked_multiply(std::int64_t a, std::int64_t b) {
  using Limits = std::numeric_limits<std::int64_t>;
  if (a == 0 || b == 0)
    return 0;
  if ((a == -1 && b == Limits::min()) || (b == -1 && a == Limits::min()))
    return std::nullopt;
  if (a > 0) {
    if ((b > 0 && a > Limits::max() / b) || (b < 0 && b < Limits::min() / a))
      return std::nullopt;
  } else {
    if ((b > 0 && a < Limits::min() / b) || (b < 0 && a < Limits::max() / b))
      return std::nullopt;
  }
  return a * b;
}

const ConstantFact &fact(const EGraph &graph, Id id) {
  return std::any_cast<const ConstantFact &>(graph.analysis_data(id));
}
} // namespace

std::any ConstantAnalysis::make(const EGraph &graph, const ENode &node) const {
  if (node.children.empty()) {
    if (const auto value = parse_integer(node.op))
      return known(*value);
    return unknown();
  }
  if ((node.op != "+" && node.op != "*") || node.children.size() != 2)
    return unknown();
  const auto &left = fact(graph, node.children[0]);
  const auto &right = fact(graph, node.children[1]);
  if (left.kind != ConstantFact::Kind::Known ||
      right.kind != ConstantFact::Kind::Known)
    return unknown();
  const auto result = node.op == "+"
                          ? checked_add(left.value, right.value)
                          : checked_multiply(left.value, right.value);
  return result ? std::any(known(*result)) : std::any(unknown());
}

AnalysisMerge ConstantAnalysis::merge(std::any &into,
                                      const std::any &from) const {
  auto &target = std::any_cast<ConstantFact &>(into);
  const auto &source = std::any_cast<const ConstantFact &>(from);
  if (target.kind == ConstantFact::Kind::Conflict ||
      source.kind == ConstantFact::Kind::Conflict)
    return AnalysisMerge::Conflict;
  if (source.kind == ConstantFact::Kind::Unknown)
    return AnalysisMerge::Unchanged;
  if (target.kind == ConstantFact::Kind::Unknown) {
    target = source;
    return AnalysisMerge::Changed;
  }
  return target.value == source.value ? AnalysisMerge::Unchanged
                                      : AnalysisMerge::Conflict;
}

void ConstantAnalysis::modify(EGraph &graph, Id id) const {
  const auto value = fact(graph, id);
  if (value.kind != ConstantFact::Kind::Known)
    return;
  const auto literal = graph.add(std::to_string(value.value));
  graph.merge(id, literal);
}

Condition known_nonzero(std::string variable) {
  if (variable.size() < 2 || variable.front() != '?')
    throw std::invalid_argument(
        "nonzero condition requires a pattern variable");
  Condition result;
  result.name = "known-nonzero";
  result.required_variables = {variable};
  result.check = [variable = std::move(variable)](const EGraph &graph, Id,
                                                  const Substitution &subst) {
    graph.require_clean();
    if (!graph.has_analysis())
      throw std::logic_error(
          "known-nonzero condition requires ConstantAnalysis");
    const auto found = subst.find(variable);
    if (found == subst.end())
      throw std::logic_error("known-nonzero condition has no bound variable");
    const auto *value =
        std::any_cast<ConstantFact>(&graph.analysis_data(found->second));
    if (!value)
      throw std::logic_error(
          "known-nonzero condition requires ConstantAnalysis facts");
    if (value->kind == ConstantFact::Kind::Conflict)
      throw AnalysisConflict(
          "nonzero condition encountered conflicting constants");
    return value->kind == ConstantFact::Kind::Known && value->value != 0;
  };
  return result;
}
} // namespace eggc
