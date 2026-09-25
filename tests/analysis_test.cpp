#include "eggc/constant_analysis.hpp"
#include "eggc/extract.hpp"
#include "eggc/parser.hpp"
#include "eggc/runner.hpp"
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
bool check(bool value, const char *description) {
  if (value)
    return true;
  std::cerr << "FAILED: " << description << '\n';
  return false;
}

bool folds_and_rewrites_end_to_end() {
  eggc::EGraph graph(std::make_shared<eggc::ConstantAnalysis>());
  const auto root = graph.add_expr(eggc::parse_expr("(+ (* 2 3) (+ x 0))"));
  graph.rebuild();
  const std::vector<eggc::Rewrite> rules{
      eggc::parse_rewrite("add-zero", "(+ ?x 0)", "?x")};
  const auto report = eggc::run(graph, rules);
  const auto best = eggc::Extractor(graph).find_best_rec_expr(root);
  return check(report.reason == eggc::StopReason::Saturated,
               "constant folding reaches saturation") &&
         check(eggc::to_string(best.second) == "(+ 6 x)",
               "folding and rewriting simplify the expression") &&
         check(best.first == 3, "extraction selects the folded expression") &&
         check(graph.analysis_revision() > 0,
               "constant analysis records propagated facts");
}

bool runner_reports_analysis_progress_during_rewrite() {
  eggc::EGraph graph(std::make_shared<eggc::ConstantAnalysis>());
  const auto a = graph.add("a");
  const auto report =
      eggc::run(graph, {eggc::parse_rewrite("a-is-three", "a", "3")});
  return check(report.reason == eggc::StopReason::Saturated,
               "analysis rewrite reaches saturation") &&
         check(!report.history.empty() &&
                   report.history[0].analysis_changes > 0,
               "runner reports analysis changes during an iteration") &&
         check(graph.find(a) == graph.find(graph.add("3")),
               "rewrite merges the known constant");
}

bool analysis_updates_parent_after_child_merge() {
  eggc::EGraph graph(std::make_shared<eggc::ConstantAnalysis>());
  const auto x = graph.add("x");
  const auto zero = graph.add("0");
  const auto sum = graph.add("+", {x, zero});
  const auto three = graph.add("3");
  graph.rebuild();
  graph.merge(x, three);
  graph.rebuild();
  const auto fact = std::any_cast<eggc::ConstantFact>(graph.analysis_data(sum));
  return check(fact.kind == eggc::ConstantFact::Kind::Known && fact.value == 3,
               "parent analysis is recomputed after child facts change");
}

bool conflicting_constants_are_rejected() {
  eggc::EGraph graph(std::make_shared<eggc::ConstantAnalysis>());
  const auto two = graph.add("2");
  const auto three = graph.add("3");
  graph.rebuild();
  bool rejected = false;
  try {
    graph.merge(two, three);
  } catch (const eggc::AnalysisConflict &) {
    rejected = true;
  }
  return check(rejected, "merging unequal known constants reports conflict") &&
         check(graph.find(two) != graph.find(three),
               "conflicting classes remain separate");
}

bool overflow_does_not_fold() {
  eggc::EGraph graph(std::make_shared<eggc::ConstantAnalysis>());
  const auto root =
      graph.add_expr(eggc::parse_expr("(+ 9223372036854775807 1)"));
  graph.rebuild();
  const auto fact =
      std::any_cast<eggc::ConstantFact>(graph.analysis_data(root));
  return check(fact.kind == eggc::ConstantFact::Kind::Unknown,
               "overflowing operation is left unknown");
}
} // namespace

int main() {
  const bool passed = folds_and_rewrites_end_to_end() &&
                      runner_reports_analysis_progress_during_rewrite() &&
                      analysis_updates_parent_after_child_merge() &&
                      conflicting_constants_are_rejected() &&
                      overflow_does_not_fold();
  return passed ? 0 : 1;
}
