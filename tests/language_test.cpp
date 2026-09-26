#include <iostream>
#include <limits>
#include <random>
#include <set>
#include <stdexcept>
#include <string>

#include "eggc/core.hpp"

namespace {
#define CHECK(expr)                                                            \
  do {                                                                         \
    if (!(expr))                                                               \
      throw std::runtime_error(#expr);                                         \
  } while (false)
template <class Exception, class F> void throws(F f) {
  bool caught = false;
  try {
    f();
  } catch (const Exception &) {
    caught = true;
  }
  CHECK(caught);
}

// This node is wholly outside eggc, uses different field names, and includes
// semantic data unrelated to TensorLang. The engine must not inspect fields.
enum class Kind { Leaf, Unknown, Add, Shift };
struct Node {
  Kind kind;
  int payload;
  std::vector<eggc::Id> args;
  using Discriminant = Kind;
  Kind discriminant() const { return kind; }
  const auto &children() const { return args; }
  auto &children_mut() { return args; }
  bool matches(const Node &n) const {
    return kind == n.kind && payload == n.payload &&
           args.size() == n.args.size();
  }
  bool operator==(const Node &n) const { return matches(n) && args == n.args; }
  std::size_t hash() const {
    std::size_t h = std::hash<int>{}(static_cast<int>(kind));
    eggc::hash_combine(h, std::hash<int>{}(payload));
    for (auto child : args)
      eggc::hash_combine(h, child);
    return h;
  }
};
using Graph = eggc::EGraph<Node>;
using Pat = eggc::Pattern<Node>;
using Rule = eggc::Rewrite<Node>;
Node leaf(int value) { return {Kind::Leaf, value, {}}; }
Pat unary(int payload, Pat child) {
  return Pat::node(Node{Kind::Shift, payload, {0}}, {std::move(child)});
}
Pat add(Pat a, Pat b) {
  return Pat::node(Node{Kind::Add, 0, {0, 0}}, {std::move(a), std::move(b)});
}
Rule commute() {
  return {"commute", add(Pat::var("a"), Pat::var("b")),
          add(Pat::var("b"), Pat::var("a"))};
}

void identity_and_congruence() {
  Graph g;
  auto a = g.add(leaf(1)), b = g.add(leaf(2));
  auto x = g.add(Node{Kind::Shift, 3, {a}});
  auto y = g.add(Node{Kind::Shift, 3, {b}});
  auto different = g.add(Node{Kind::Shift, 4, {a}});
  auto parent = g.add(Node{Kind::Add, 0, {x, different}});
  auto other_parent = g.add(Node{Kind::Add, 0, {y, different}});
  CHECK(x == g.add(Node{Kind::Shift, 3, {a}}));
  CHECK(x != different);
  auto count = g.node_count();
  throws<std::out_of_range>(
      [&] { g.add(Node{Kind::Shift, 3, {eggc::invalid_id}}); });
  CHECK(g.node_count() == count);
  throws<std::logic_error>([&] { g.nodes(x); });
  g.merge(a, b);
  g.rebuild();
  CHECK(g.find(x) == g.find(y));
  CHECK(g.find(parent) == g.find(other_parent));
  CHECK(g.find(x) != g.find(different));
  CHECK(g.classes_for_op(Kind::Shift).size() == 2);
  CHECK(g.node_count() == 5);
  auto revision = g.revision();
  g.rebuild();
  CHECK(g.revision() == revision);
}

void exact_patterns_and_backtracking() {
  Graph g;
  auto a = g.add(leaf(1)), b = g.add(leaf(2));
  auto f1 = g.add(Node{Kind::Shift, 3, {a}});
  auto f2 = g.add(Node{Kind::Shift, 4, {a}});
  g.merge(f1, f2);
  auto ab = g.add(Node{Kind::Add, 0, {a, b}});
  auto aa = g.add(Node{Kind::Add, 0, {a, a}});
  g.rebuild();
  CHECK(eggc::match(g, unary(3, Pat::var("x")), f1).size() == 1);
  CHECK(eggc::match(g, unary(5, Pat::var("x")), f1).empty());
  auto repeated = add(Pat::var("x"), Pat::var("x"));
  CHECK(eggc::match(g, repeated, ab).empty());
  CHECK(eggc::match(g, repeated, aa).size() == 1);
  auto subst = eggc::match(g, add(Pat::var("x"), Pat::var("y")), ab).front();
  auto before = g.node_count();
  throws<std::out_of_range>(
      [&] { eggc::instantiate(g, add(Pat::var("x"), Pat::var("z")), subst); });
  CHECK(g.node_count() == before);
  auto rhs = eggc::instantiate(g, unary(7, Pat::var("x")), subst);
  g.rebuild();
  CHECK(g.nodes(rhs).front().payload == 7);
  CHECK(g.nodes(rhs).front().args.front() == a);
  // Two distinct structural paths yielding the same bindings are deduplicated.
  g.merge(a, b);
  g.merge(aa, ab);
  g.rebuild();
  CHECK(eggc::match(g, add(Pat::var("x"), Pat::var("y")), aa).size() == 1);
  bool emitted = false;
  CHECK(!eggc::search_matches(g, repeated, aa, [&](const eggc::Substitution &) {
    emitted = true;
    return false;
  }));
  CHECK(emitted);
  throws<std::invalid_argument>(
      [&] { Pat::node(Node{Kind::Add, 0, {0, 0}}, {Pat::var("x")}); });
  Pat invalid{{Node{Kind::Shift, 0, {0}}}};
  throws<std::invalid_argument>([&] { eggc::match(g, invalid, aa); });
}

void extraction_and_expressions() {
  Graph g;
  eggc::RecExpr<Node> input;
  auto a = input.add(leaf(8));
  auto f = input.add(Node{Kind::Shift, 2, {a}});
  input.add(Node{Kind::Add, 0, {f, f}});
  auto root = g.add_expr(input);
  g.rebuild();
  eggc::Extractor<Node> extractor(g);
  auto [cost, expr] = extractor.find_best(root);
  CHECK(cost == 5);
  CHECK(expr.nodes.size() == 3);
  CHECK(expr.nodes.back().args[0] == expr.nodes.back().args[1]);
  CHECK(expr.nodes[1].payload == 2);
  Graph copied;
  auto copied_root = copied.add_expr(expr);
  copied.rebuild();
  CHECK(eggc::Extractor<Node>(copied).best_cost(copied_root) == cost);
  throws<std::invalid_argument>(
      [&] { input.add(Node{Kind::Shift, 1, {input.root() + 1}}); });
  eggc::RecExpr<Node> invalid{{leaf(1), Node{Kind::Shift, 2, {1}}}};
  auto before = g.node_count();
  throws<std::invalid_argument>([&] { g.add_expr(invalid); });
  CHECK(g.node_count() == before);
  g.add(leaf(99));
  throws<std::logic_error>([&] { extractor.best_cost(root); });
  g.rebuild();
  throws<std::logic_error>([&] { extractor.find_best(root); });
  throws<std::invalid_argument>([&] {
    eggc::Extractor<Node> bad(g,
                              [](const Node &, const std::vector<std::size_t> &)
                                  -> std::optional<std::size_t> { return 0; });
  });
  CHECK(!eggc::ast_size_cost<Node>()(
      leaf(0), {std::numeric_limits<std::size_t>::max()}));
  CHECK(!eggc::ast_depth_cost<Node>()(
      leaf(0), {std::numeric_limits<std::size_t>::max()}));
  auto cyclic = g.add(Node{Kind::Shift, 9, {root}});
  g.merge(cyclic, root);
  g.rebuild();
  CHECK(eggc::Extractor<Node>(g).best_cost(root) == 5);
  throws<std::runtime_error>([&] {
    eggc::Extractor<Node> no_finite(
        g,
        [](const Node &, const std::vector<std::size_t> &)
            -> std::optional<std::size_t> { return std::nullopt; });
    no_finite.best_cost(root);
  });
}

struct ValueAnalysis;
using ValueGraph = eggc::EGraph<Node, ValueAnalysis>;
struct ValueAnalysis {
  using Data = std::optional<int>;
  Data make(const ValueGraph &g, const Node &node) const {
    if (node.kind == Kind::Unknown)
      return {};
    if (node.kind == Kind::Leaf)
      return node.payload;
    auto value = g.analysis_data(node.args.at(0));
    if (!value)
      return {};
    if (node.kind == Kind::Shift)
      return *value + node.payload;
    auto rhs = g.analysis_data(node.args.at(1));
    return rhs ? Data{*value + *rhs} : Data{};
  }
  eggc::AnalysisMerge merge(Data &into, const Data &from) const {
    if (!from || into == from)
      return eggc::AnalysisMerge::Unchanged;
    if (into)
      return eggc::AnalysisMerge::Conflict;
    into = from;
    return eggc::AnalysisMerge::Changed;
  }
};

void typed_analysis_and_conflicts() {
  ValueGraph g;
  auto unknown = g.add(Node{Kind::Unknown, 0, {}});
  auto parent = g.add(Node{Kind::Shift, 4, {unknown}});
  auto known = g.add(leaf(3));
  auto other = g.add(leaf(5));
  CHECK(!g.analysis_data(parent));
  g.merge(unknown, known);
  g.rebuild();
  CHECK(g.analysis_data(parent) == 7);
  auto before = g.revision();
  throws<eggc::AnalysisConflict>([&] { g.merge(known, other); });
  CHECK(g.revision() == before);
  CHECK(g.find(known) != g.find(other));
  // Canonical root swaps must not lose merged analysis data.
  auto another = g.add(Node{Kind::Unknown, 1, {}});
  g.merge(other, another);
  auto unknown2 = g.add(Node{Kind::Unknown, 2, {}});
  g.merge(unknown2, other);
  CHECK(g.analysis_data(unknown2) == 5);
  using VRule = eggc::Rewrite<Node, ValueAnalysis>;
  VRule rule{"commute", add(Pat::var("x"), Pat::var("y")),
             add(Pat::var("y"), Pat::var("x"))};
  auto sum = g.add(Node{Kind::Add, 0, {known, other}});
  CHECK(eggc::run(g, std::vector<VRule>{rule}).reason ==
        eggc::StopReason::Saturated);
  CHECK(g.analysis_data(sum) == 8);
}

void runner_and_guards() {
  Graph g;
  auto a = g.add(leaf(1)), b = g.add(leaf(2));
  auto root = g.add(Node{Kind::Add, 0, {a, b}});
  auto rule = commute();
  auto report = eggc::run(g, std::vector<Rule>{rule});
  CHECK(report.reason == eggc::StopReason::Saturated);
  CHECK(report.history.front().applications == 1);
  auto reversed = g.add(Node{Kind::Add, 0, {b, a}});
  CHECK(g.find(root) == g.find(reversed));
  Rule unbound{"bad", Pat::var("x"), Pat::var("y")};
  throws<std::invalid_argument>(
      [&] { eggc::run(g, std::vector<Rule>{unbound}); });
  auto guarded = commute();
  guarded.condition = eggc::Condition<Node>{
      "reject",
      {"?a"},
      [](const Graph &snapshot, eggc::Id, const eggc::Substitution &) {
        CHECK(snapshot.is_clean());
        return false;
      }};
  auto rejected = eggc::run(g, std::vector<Rule>{guarded});
  CHECK(rejected.reason == eggc::StopReason::Saturated);
  CHECK(rejected.history[0].condition_rejections == 2);
  guarded.condition->required_variables = {"?unbound"};
  throws<std::invalid_argument>(
      [&] { eggc::run(g, std::vector<Rule>{guarded}); });
  Rule variable_root{"identity", Pat::var("x"), Pat::var("x")};
  CHECK(eggc::run(g, std::vector<Rule>{variable_root}).reason ==
        eggc::StopReason::Saturated);
}

void runner_limits() {
  Graph bounded;
  bounded.add(leaf(1));
  Rule stopped{"bounded-custom-search",
               [](const Graph &, const Rule::Sink &, const eggc::StopCheck &) {
                 return false;
               }};
  CHECK(eggc::run(bounded, std::vector<Rule>{stopped}).reason ==
        eggc::StopReason::SearchLimit);
  CHECK(bounded.node_count() == 1);
  CHECK(bounded.is_clean());
  Rule rejected{"rejected-custom-match",
                [](const Graph &, const Rule::Sink &emit,
                   const eggc::StopCheck &) { return emit({0, {}, false}); }};
  auto rejection = eggc::run(bounded, std::vector<Rule>{rejected});
  CHECK(rejection.history.front().matches == 1);
  CHECK(rejection.history.front().condition_checks == 1);
  CHECK(rejection.history.front().condition_rejections == 1);
  CHECK(rejection.history.front().applications == 0);
  eggc::RunOptions rejected_limit;
  rejected_limit.match_limit = 0;
  CHECK(
      eggc::run(bounded, std::vector<Rule>{rejected}, rejected_limit).reason ==
      eggc::StopReason::MatchLimit);
  Graph g;
  auto a = g.add(leaf(1)), b = g.add(leaf(2));
  auto root = g.add(Node{Kind::Add, 0, {a, b}});
  eggc::RunOptions options;
  options.match_limit = 0;
  auto initial = g.node_count();
  auto report = eggc::run(g, std::vector<Rule>{commute()}, options);
  CHECK(report.reason == eggc::StopReason::MatchLimit);
  CHECK(g.node_count() == initial);
  CHECK(g.is_clean());
  options.match_limit.reset();
  options.time_limit = std::chrono::milliseconds(0);
  CHECK(eggc::run(g, std::vector<Rule>{commute()}, options).reason ==
        eggc::StopReason::TimeLimit);
  options.time_limit.reset();
  options.node_limit = initial + 1;
  CHECK(eggc::run(g, std::vector<Rule>{commute()}, options).reason ==
        eggc::StopReason::NodeLimit);
  CHECK(g.is_clean());
  CHECK(g.find(root) == g.find(g.add(Node{Kind::Add, 0, {b, a}})));
  options.node_limit = 10000;
  options.iteration_limit = 0;
  CHECK(eggc::run(g, std::vector<Rule>{commute()}, options).reason ==
        eggc::StopReason::IterationLimit);
  options.iteration_limit = 10;
  options.per_rule_match_limit = 1;
  report = eggc::run(g, std::vector<Rule>{commute()}, options);
  CHECK(report.reason == eggc::StopReason::Saturated);
  CHECK(report.history.front().backed_off_rules == 1);
  options.per_rule_match_limit = 0;
  throws<std::invalid_argument>(
      [&] { eggc::run(g, std::vector<Rule>{commute()}, options); });
}

void custom_search_preserves_attrs_and_limits() {
  Graph g;
  auto a = g.add(leaf(1)), b = g.add(leaf(2));
  auto x = g.add(Node{Kind::Shift, 3, {a}});
  auto y = g.add(Node{Kind::Shift, 4, {a}});
  g.merge(x, y);
  std::set<int> captured;
  Rule copy{
      "copy", [&, b](const Graph &snapshot, const Rule::Sink &emit,
                     const eggc::StopCheck &stop) {
        CHECK(snapshot.is_clean());
        for (auto root : snapshot.classes_for_op(Kind::Shift)) {
          for (Node node : snapshot.nodes(root)) {
            if (stop && stop())
              return false;
            captured.insert(node.payload);
            node.args = {b};
            if (!emit({root, [node](Graph &target) -> std::optional<eggc::Id> {
                         return target.add(node);
                       }}))
              return false;
          }
        }
        return true;
      }};
  eggc::RunOptions options;
  options.match_limit = 1;
  auto before = g.node_count();
  CHECK(eggc::run(g, std::vector<Rule>{copy}, options).reason ==
        eggc::StopReason::MatchLimit);
  // An incomplete search must discard ALL queued actions.
  CHECK(g.node_count() == before);
  options.match_limit.reset();
  options.iteration_limit = 1;
  eggc::run(g, std::vector<Rule>{copy}, options);
  CHECK(captured == std::set<int>({3, 4}));
  CHECK(g.nodes(x).size() == 4);
  CHECK(g.find(x) == g.find(g.add(Node{Kind::Shift, 3, {b}})));
  CHECK(g.find(x) == g.find(g.add(Node{Kind::Shift, 4, {b}})));
  Rule skipped{"skip", [x](const Graph &, const Rule::Sink &emit,
                           const eggc::StopCheck &) {
                 return emit({x, [](Graph &) -> std::optional<eggc::Id> {
                                return std::nullopt;
                              }});
               }};
  CHECK(eggc::run(g, std::vector<Rule>{skipped}).reason ==
        eggc::StopReason::Saturated);
}

// Independent quadratic congruence-closure oracle. This compares semantics of
// rebuild against a full scan, rather than testing the production worklist.
void randomized_congruence_oracle() {
  std::mt19937 rng(12345);
  for (int trial = 0; trial < 30; ++trial) {
    Graph g;
    std::vector<Node> terms;
    std::vector<eggc::Id> graph_ids, parent;
    auto find = [&](eggc::Id x) {
      while (parent[x] != x)
        x = parent[x];
      return x;
    };
    auto merge = [&](eggc::Id a, eggc::Id b) { parent[find(b)] = find(a); };
    for (eggc::Id i = 0; i < 70; ++i) {
      Node n = leaf(i % 7);
      if (i >= 7) {
        n = Node{Kind::Shift,
                 static_cast<int>(rng() % 4),
                 {static_cast<eggc::Id>(rng() % i)}};
        if (rng() % 2) {
          n.kind = Kind::Add;
          n.args.push_back(static_cast<eggc::Id>(rng() % i));
        }
      }
      terms.push_back(n);
      parent.push_back(i);
      for (auto &child : n.args)
        child = graph_ids[child];
      graph_ids.push_back(g.add(n));
    }
    for (int i = 0; i < 15; ++i) {
      auto a = static_cast<eggc::Id>(rng() % terms.size());
      auto b = static_cast<eggc::Id>(rng() % terms.size());
      merge(a, b);
      g.merge(graph_ids[a], graph_ids[b]);
    }
    bool changed;
    do {
      changed = false;
      for (eggc::Id a = 0; a < terms.size(); ++a)
        for (eggc::Id b = 0; b < a; ++b) {
          if (find(a) == find(b) || !terms[a].matches(terms[b]))
            continue;
          bool equal = true;
          for (std::size_t c = 0; c < terms[a].args.size(); ++c)
            equal &= find(terms[a].args[c]) == find(terms[b].args[c]);
          if (equal) {
            merge(a, b);
            changed = true;
          }
        }
    } while (changed);
    g.rebuild();
    for (eggc::Id a = 0; a < terms.size(); ++a)
      for (eggc::Id b = 0; b < a; ++b)
        CHECK((find(a) == find(b)) ==
              (g.find(graph_ids[a]) == g.find(graph_ids[b])));
    for (auto root : g.classes())
      for (const Node &node : g.nodes(root))
        for (auto child : node.args)
          CHECK(child == g.find(child));
  }
}
} // namespace
int main() {
  try {
    identity_and_congruence();
    exact_patterns_and_backtracking();
    extraction_and_expressions();
    typed_analysis_and_conflicts();
    runner_and_guards();
    runner_limits();
    custom_search_preserves_attrs_and_limits();
    randomized_congruence_oracle();
    std::cout << "8 external-language regression groups passed\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
