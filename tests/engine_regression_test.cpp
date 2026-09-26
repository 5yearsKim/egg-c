#include <eggc/all.hpp>
#include <iostream>
#include <memory>
#include <random>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace {
using Node = eggc::SymbolLang;
using Graph = eggc::EGraph<Node>;
using Pattern = eggc::Pattern<Node>;
using Rule = eggc::Rewrite<Node>;

void check(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
template <class F>
void invalid(F fn) {
  try {
    fn();
  } catch (const std::invalid_argument&) {
    return;
  }
  throw std::runtime_error("expected invalid_argument");
}

struct BooleanFacts {
  using Data = bool;
  std::shared_ptr<std::size_t> evaluations;
  Data make(const eggc::EGraph<Node, BooleanFacts>& graph,
            const Node& node) const {
    ++*evaluations;
    bool result = node.op == "known";
    for (auto child : node.args) result = result || graph.analysis_data(child);
    return result;
  }
  eggc::AnalysisMerge merge(Data& into, const Data& from) const {
    if (into || !from) return eggc::AnalysisMerge::Unchanged;
    into = true;
    return eggc::AnalysisMerge::Changed;
  }
};
using BooleanGraph = eggc::EGraph<Node, BooleanFacts>;
static_assert(eggc::AnalysisFor<BooleanFacts, Node>);
static_assert(std::is_same_v<
              decltype(std::declval<const BooleanGraph&>().analysis_data(0)),
              const bool&>);

void boolean_data_and_cycles() {
  auto calls = std::make_shared<std::size_t>(0);
  BooleanGraph graph(BooleanFacts{calls});
  auto a = graph.add(Node::leaf("a"));
  auto b = graph.add(Node::leaf("b"));
  auto known = graph.add(Node::leaf("known"));
  auto parent = graph.add(Node::node("f", {a, a}));
  graph.merge(a, b);  // Raise a's rank before merging into it from known.
  const auto facts_before = graph.analysis_revision();
  graph.merge(known, a);
  check(graph.analysis_data(a), "root swap lost Boolean facts");
  check(graph.analysis_revision() == facts_before + 1,
        "root swap did not record the survivor's changed facts");
  graph.merge(a, parent);  // A self-cycle with repeated child references.
  graph.rebuild();
  check(graph.analysis_data(parent), "cyclic propagation lost known fact");
  check(graph.is_clean(), "cyclic rebuild did not complete");
  const auto revision = graph.revision();
  *calls = 0;
  graph.rebuild();
  check(*calls == 0 && graph.last_rebuild_stats().analysis_evaluations == 0,
        "clean rebuild reevaluated analysis");
  check(graph.revision() == revision, "clean rebuild changed revision");

  BooleanGraph unknown(BooleanFacts{calls});
  auto x = unknown.add(Node::leaf("x"));
  unknown.merge(x, unknown.add(Node::node("f", {x})));
  unknown.rebuild();
  check(!unknown.analysis_data(x), "unknown cycle invented a known fact");
}

std::size_t chain_work(std::size_t size) {
  auto calls = std::make_shared<std::size_t>(0);
  BooleanGraph graph(BooleanFacts{calls});
  std::vector<eggc::Id> ids;
  for (std::size_t i = 0; i <= size; ++i)
    ids.push_back(graph.add(Node::leaf("x" + std::to_string(i))));
  for (std::size_t i = 0; i < size; ++i)
    graph.merge(ids[i], graph.add(Node::node("f", {ids[i + 1]})));
  graph.rebuild();
  graph.merge(ids.back(), graph.add(Node::leaf("known")));
  *calls = 0;
  graph.rebuild();
  for (auto id : ids)
    check(graph.analysis_data(id), "chain fact did not propagate");
  check(graph.last_rebuild_stats().analysis_evaluations == *calls,
        "analysis evaluation counter is inaccurate");
  check(graph.last_rebuild_stats().analysis_changes == size,
        "analysis change counter is inaccurate");
  check(graph.last_rebuild_stats().congruence_passes == 1,
        "analysis changes reran congruence closure");
  check(*calls <= 4 * (size + 1),
        "reverse chain requires superlinear analysis work");
  return *calls;
}

void analysis_scaling() {
  auto small = chain_work(100);
  auto large = chain_work(200);
  check(large <= small * 5 / 2, "doubling the chain requires quadratic work");
}

void reachable_pattern_entries() {
  Pattern disconnected{{eggc::Var{"?missing"}, Node::leaf("a")}};
  invalid([&] { disconnected.validate(); });
  Graph graph;
  auto a = graph.add(Node::leaf("a"));
  auto pair = graph.add(Node::node("pair", {a, a}));
  graph.rebuild();
  const auto revision = graph.revision();
  Rule unbound{"bad", disconnected, Pattern::var("missing")};
  invalid([&] { eggc::run(graph, std::vector{unbound}); });
  invalid([&] { eggc::instantiate(graph, disconnected, {{"?missing", a}}); });
  check(graph.revision() == revision, "invalid pattern mutated the graph");
  // Both child positions share one reachable pattern entry.
  Pattern shared{{eggc::Var{"?x"}, Node::node("pair", {0, 0})}};
  shared.validate();
  check(eggc::match(graph, shared, pair).size() == 1,
        "shared pattern was rejected");
  check(graph.find(eggc::instantiate(graph, shared, {{"?x", a}})) ==
            graph.find(pair),
        "shared pattern instantiation changed bindings");
}

void backoff_and_diagnostics() {
  Graph graph;
  auto a = graph.add(Node::leaf("a"));
  graph.add(Node::leaf("b"));
  auto rules = std::vector{eggc::rewrite("duplicate", "a", "(g a)"),
                           eggc::rewrite("duplicate", "?x", "(f ?x)"),
                           eggc::rewrite("duplicate", "b", "(h b)")};
  eggc::RunOptions options;
  options.iteration_limit = 1;
  options.per_rule_match_limit = 1;
  options.collect_rule_stats = true;
  auto report = eggc::run(graph, rules, options);
  const auto& stats = report.history.front();
  check(stats.backed_off_rules == 1 && stats.applications == 2,
        "backoff did not discard only the offending rule");
  check(graph.node_count() == 4 && graph.classes_for_op("f").empty(),
        "backed-off expanding rule modified the graph");
  check(stats.rules.size() == 3, "missing per-rule diagnostics");
  for (std::size_t i = 0; i < stats.rules.size(); ++i) {
    check(stats.rules[i].rule_index == i && stats.rules[i].name == "duplicate",
          "duplicate rule names were conflated");
    check(stats.rules[i].searched, "rule was not searched");
  }
  check(stats.rules[0].applications == 1 && stats.rules[2].applications == 1,
        "unaffected rules did not apply");
  check(stats.rules[1].backed_off && !stats.rules[1].search_completed &&
            stats.rules[1].matches == 1 && stats.rules[1].applications == 0,
        "backoff diagnostics are inaccurate");
  check(report.initial_rebuild.analysis_evaluations == 2,
        "initial rebuild was not reported");
  auto identity =
      eggc::run(graph, std::vector{eggc::rewrite("identity", "?x", "?x")});
  check(identity.history.front().rules.empty(),
        "disabled diagnostics allocated rule stats");

  // A custom searcher receives identical backoff behavior.
  Rule custom{"custom", [a](const Graph& snapshot, const Rule::Sink& emit,
                            const eggc::StopCheck&) {
                for (auto id : snapshot.classes()) {
                  if (!emit({id, [a](Graph& target) -> std::optional<eggc::Id> {
                               return target.add(Node::node("custom", {a}));
                             }}))
                    return false;
                }
                return true;
              }};
  const auto count = graph.node_count();
  auto custom_report = eggc::run(graph, std::vector{custom}, options);
  check(custom_report.history.front().applications == 0 &&
            graph.node_count() == count,
        "custom backed-off rule retained partial applications");

  // Deferred rules must retry; an unchanged iteration is not saturation.
  Graph retry;
  retry.add(Node::leaf("a"));
  retry.add(Node::leaf("b"));
  options.iteration_limit = 10;
  auto retried = eggc::run(
      retry, std::vector{eggc::rewrite("identity", "?x", "?x")}, options);
  check(
      retried.reason == eggc::StopReason::Saturated && retried.iterations == 2,
      "backoff incorrectly reported saturation before retry");
}

void limit_and_condition_diagnostics() {
  Graph graph;
  graph.add(Node::leaf("a"));
  graph.add(Node::leaf("b"));
  auto guarded = eggc::rewrite("reject", "?x", "?x");
  guarded.condition = eggc::Condition<Node>{
      "false", {"?x"}, [](const Graph&, eggc::Id, const eggc::Substitution&) {
        return false;
      }};
  eggc::RunOptions options;
  options.collect_rule_stats = true;
  auto report = eggc::run(graph, std::vector{guarded}, options);
  check(report.history.front().rules[0].condition_checks == 2 &&
            report.history.front().rules[0].condition_rejections == 2,
        "condition diagnostics are inaccurate");
  options.match_limit = 1;
  auto count = graph.node_count();
  auto limited = eggc::run(
      graph, std::vector{eggc::rewrite("expand", "?x", "(f ?x)"), guarded},
      options);
  check(limited.reason == eggc::StopReason::MatchLimit &&
            graph.node_count() == count,
        "global match limit applied partial work");
  check(limited.history.front().rules[0].applications == 0 &&
            !limited.history.front().rules[1].searched,
        "partial search diagnostics are inaccurate");

  options.match_limit.reset();
  options.per_rule_match_limit = 1;
  auto rejected_backoff = eggc::run(graph, std::vector{guarded}, options);
  check(rejected_backoff.history.front().rules[0].backed_off &&
            rejected_backoff.history.front().rules[0].condition_checks == 1,
        "rejected conditions did not consume backoff budget");
}

unsigned literal_fact(const Node& node) {
  if (node.op.size() == 2 && node.op[0] == 'k') return 1u << (node.op[1] - '0');
  return 0;
}
struct Bits {
  using Data = unsigned;
  Data make(const eggc::EGraph<Node, Bits>& graph, const Node& node) const {
    auto result = literal_fact(node);
    for (auto child : node.args) result |= graph.analysis_data(child);
    return result;
  }
  eggc::AnalysisMerge merge(Data& into, const Data& from) const {
    auto result = into | from;
    if (result == into) return eggc::AnalysisMerge::Unchanged;
    into = result;
    return eggc::AnalysisMerge::Changed;
  }
};

// Independent full-scan fixed-point oracle over original nodes, including
// nodes retired by hash-consing and congruence. Only class membership comes
// from the production graph; this checks propagation, not union-find.
void randomized_analysis_oracle() {
  std::mt19937 rng(417);
  for (int trial = 0; trial < 30; ++trial) {
    eggc::EGraph<Node, Bits> graph;
    std::vector<Node> terms;
    std::vector<eggc::Id> ids;
    for (int batch = 0; batch < 4; ++batch) {
      for (int i = 0; i < 15; ++i) {
        Node original = Node::leaf("k" + std::to_string(rng() % 8));
        if (!terms.empty() && rng() % 3 != 0) {
          original =
              Node::node("f", {static_cast<eggc::Id>(rng() % terms.size())});
          if (rng() % 2)
            original.args.push_back(
                static_cast<eggc::Id>(rng() % terms.size()));
        }
        terms.push_back(original);
        for (auto& child : original.args) child = ids[child];
        ids.push_back(graph.add(std::move(original)));
      }
      for (int i = 0; i < 5; ++i)
        graph.merge(ids[rng() % ids.size()], ids[rng() % ids.size()]);
      graph.rebuild();
      std::vector<unsigned> expected(terms.size());
      bool changed;
      do {
        changed = false;
        for (std::size_t i = 0; i < terms.size(); ++i) {
          auto inferred = literal_fact(terms[i]);
          for (auto child : terms[i].args)
            inferred |= expected[graph.find(ids[child])];
          auto& fact = expected[graph.find(ids[i])];
          if ((fact | inferred) != fact) {
            fact |= inferred;
            changed = true;
          }
        }
      } while (changed);
      for (auto id : ids)
        check(graph.analysis_data(id) == expected[graph.find(id)],
              "analysis disagrees with oracle");
    }
  }
}
}  // namespace

int main() {
  try {
    boolean_data_and_cycles();
    analysis_scaling();
    reachable_pattern_entries();
    backoff_and_diagnostics();
    limit_and_condition_diagnostics();
    randomized_analysis_oracle();
    std::cout << "Engine correctness and incremental analysis checks passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
