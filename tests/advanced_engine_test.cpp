#include <charconv>
#include <cmath>
#include <eggc/all.hpp>
#include <iostream>
#include <limits>
#include <memory>
#include <random>
#include <set>
#include <stdexcept>

namespace {
using Node = eggc::SymbolLang;
using Graph = eggc::EGraph<Node>;
using Pattern = eggc::Pattern<Node>;
void check(bool test, const char *message) {
  if (!test)
    throw std::runtime_error(message);
}
template <class Error, class F> void throws(F fn) {
  try {
    fn();
  } catch (const Error &) {
    return;
  }
  throw std::runtime_error("expected exception");
}
struct Values {
  using Data = std::optional<int>;
  Data make(const eggc::EGraph<Node, Values> &graph, const Node &node) const {
    if (node.args.empty()) {
      int value = 0;
      const auto [end, error] = std::from_chars(
          node.op.data(), node.op.data() + node.op.size(), value);
      if (error == std::errc{} && end == node.op.data() + node.op.size())
        return value;
    }
    if (node.op == "+" && node.args.size() == 2) {
      auto a = graph.analysis_data(node.args[0]),
           b = graph.analysis_data(node.args[1]);
      if (a && b) {
        const long long sum = static_cast<long long>(*a) + *b;
        if (sum >= std::numeric_limits<int>::min() &&
            sum <= std::numeric_limits<int>::max())
          return static_cast<int>(sum);
      }
    }
    return {};
  }
  eggc::AnalysisMerge merge(Data &into, const Data &from) const {
    if (!from || into == from)
      return eggc::AnalysisMerge::Unchanged;
    if (into)
      return eggc::AnalysisMerge::Conflict;
    into = from;
    return eggc::AnalysisMerge::Changed;
  }
  void modify(eggc::EGraph<Node, Values> &graph, eggc::Id id) {
    const auto value =
        graph.analysis_data(id); // Copy across add() reallocation.
    if (value)
      graph.merge(id, graph.add(Node::leaf(std::to_string(*value))));
  }
};
struct Growing {
  using Data = std::monostate;
  Data make(const eggc::EGraph<Node, Growing> &, const Node &) const {
    return {};
  }
  eggc::AnalysisMerge merge(Data &, const Data &) const {
    return eggc::AnalysisMerge::Unchanged;
  }
  void modify(eggc::EGraph<Node, Growing> &graph, eggc::Id id) {
    graph.add(Node::node("f", {id}));
  }
};
struct CountFacts {
  using Data = bool;
  std::shared_ptr<std::size_t> calls;
  Data make(const eggc::EGraph<Node, CountFacts> &g, const Node &node) const {
    ++*calls;
    if (node.op == "known")
      return true;
    for (auto child : node.args)
      if (g.analysis_data(child))
        return true;
    return false;
  }
  eggc::AnalysisMerge merge(Data &into, const Data &from) const {
    if (into || !from)
      return eggc::AnalysisMerge::Unchanged;
    into = true;
    return eggc::AnalysisMerge::Changed;
  }
};
struct RetryingHook {
  using Data = std::monostate;
  std::shared_ptr<bool> fail;
  Data make(const eggc::EGraph<Node, RetryingHook> &, const Node &) const {
    return {};
  }
  eggc::AnalysisMerge merge(Data &, const Data &) const {
    return eggc::AnalysisMerge::Unchanged;
  }
  void modify(eggc::EGraph<Node, RetryingHook> &graph, eggc::Id id) {
    if (*fail) {
      *fail = false;
      throw std::runtime_error("transient hook failure");
    }
    graph.merge(id, graph.add(Node::leaf("done")));
  }
};
struct RecursiveHook {
  using Data = std::monostate;
  Data make(const eggc::EGraph<Node, RecursiveHook> &, const Node &) const {
    return {};
  }
  eggc::AnalysisMerge merge(Data &, const Data &) const {
    return eggc::AnalysisMerge::Unchanged;
  }
  void modify(eggc::EGraph<Node, RecursiveHook> &graph, eggc::Id) {
    graph.rebuild();
  }
};
void hook_exceptions() {
  eggc::EGraph<Node, RetryingHook> retry(
      RetryingHook{std::make_shared<bool>(true)});
  retry.add(Node::leaf("start"));
  throws<std::runtime_error>([&] { retry.rebuild(); });
  check(!retry.is_clean(), "throwing hook left graph marked clean");
  retry.rebuild();
  retry.check_invariants();
  check(retry.lookup(Node::leaf("done")).has_value(),
        "failed hook work was lost");
  eggc::EGraph<Node, RecursiveHook> recursive;
  recursive.add(Node::leaf("start"));
  throws<std::logic_error>([&] { recursive.rebuild(); });
}
struct LooseNode : Node {
  static constexpr bool exact_matches = false;
  using Discriminant = std::size_t;
  Discriminant discriminant() const { return args.size(); }
  bool matches(const LooseNode &other) const {
    return args.size() == other.args.size() && (op == "wild" || op == other.op);
  }
};
void nonexact_matching() {
  eggc::EGraph<LooseNode> graph;
  auto a = graph.add(LooseNode{Node::leaf("a")});
  auto b = graph.add(LooseNode{Node::leaf("b")});
  graph.rebuild();
  eggc::Pattern<LooseNode> pattern{{LooseNode{Node::leaf("wild")}}};
  check(eggc::match(graph, pattern, a).size() == 1 &&
            eggc::match(graph, pattern, b).size() == 1,
        "compiled lookup changed non-exact matches semantics");
}
void incremental_components() {
  auto calls = std::make_shared<std::size_t>(0);
  eggc::EGraph<Node, CountFacts> graph(CountFacts{calls});
  for (int i = 0; i < 2000; ++i) {
    auto id = graph.add(Node::leaf("unused" + std::to_string(i)));
    graph.add(Node::node("unused-parent", {id}));
  }
  auto x = graph.add(Node::leaf("x"));
  auto parent = graph.add(Node::node("f", {x, x}));
  graph.rebuild();
  graph.check_invariants();
  *calls = 0;
  graph.merge(x, graph.add(Node::leaf("known")));
  const auto before = *calls;
  graph.rebuild();
  check(graph.analysis_data(parent), "isolated update did not propagate");
  check(*calls - before <= 2,
        "isolated update scanned unrelated analysis nodes");
  check(graph.last_rebuild_stats().repaired_nodes <= 3,
        "isolated update repaired unrelated nodes");
  check(graph.last_rebuild_stats().refreshed_classes <= 3,
        "isolated update refreshed unrelated views");
  graph.check_invariants();
}
void analysis_hooks_and_limits() {
  eggc::EGraph<Node, Values> graph;
  graph.enable_explanations();
  auto root = graph.add_expr(eggc::parse_expr("(+ (+ 2 3) 4)"));
  graph.rebuild();
  graph.check_invariants();
  auto [cost, expr] = eggc::Extractor<Node, Values>(graph).find_best(root);
  check(cost == 1 && eggc::to_string(expr) == "9",
        "analysis hook did not fold nested constants");
  auto nine = graph.lookup(Node::leaf("9"));
  check(nine && graph.find(root) == *nine,
        "constant hook did not insert literal");
  const auto revision = graph.revision();
  graph.rebuild();
  check(graph.revision() == revision &&
            graph.last_rebuild_stats().modifications == 0,
        "idempotent analysis hook did not terminate");

  eggc::EGraph<Node, Growing> growing;
  auto base = growing.add(Node::leaf("base"));
  eggc::RunOptions options;
  options.node_limit = 12;
  auto stopped =
      eggc::run(growing, std::vector<eggc::Rewrite<Node, Growing>>{}, options);
  check(stopped.reason == eggc::StopReason::NodeLimit &&
            growing.node_count() == 12 && growing.is_clean(),
        "expanding analysis hook ignored node budget");
  growing.check_invariants();
  check(eggc::Extractor<Node, Growing>(growing).best_cost(base) == 1,
        "budgeted hook rebuild left graph unqueryable");
  options.node_limit = 15;
  auto resumed =
      eggc::run(growing, std::vector<eggc::Rewrite<Node, Growing>>{}, options);
  check(resumed.reason == eggc::StopReason::NodeLimit &&
            growing.node_count() == 15,
        "suspended hooks did not resume");
  options.node_limit = 100;
  options.time_limit = std::chrono::milliseconds(0);
  auto timed =
      eggc::run(growing, std::vector<eggc::Rewrite<Node, Growing>>{}, options);
  check(timed.reason == eggc::StopReason::TimeLimit &&
            growing.node_count() == 15,
        "zero time budget executed pending hook");
}
void lookup_hooks_and_provenance() {
  Graph graph;
  graph.enable_explanations();
  auto root = graph.add_expr(eggc::parse_expr("(+ a 0)"));
  throws<std::logic_error>([&] { graph.lookup(Node::leaf("a")); });
  graph.rebuild();
  auto before = graph.revision();
  check(graph.lookup_expr(eggc::parse_expr("(+ a 0)")) == graph.find(root),
        "expression lookup failed");
  check(!graph.lookup_expr(eggc::parse_expr("(+ b 0)")),
        "lookup inserted missing expression");
  check(graph.revision() == before, "lookup mutated graph");
  std::vector<eggc::Rewrite<Node>> rules{
      eggc::rewrite("zero", "(+ ?x 0)", "?x")};
  auto a = graph.lookup(Node::leaf("a"));
  eggc::run(graph, rules);
  auto proof = graph.explain_equivalence(root, *a);
  check(!proof.empty() &&
            proof.front().justification.kind == eggc::UnionKind::Rewrite &&
            proof.front().justification.name == "zero",
        "rewrite provenance was lost");
  auto fa = graph.add(Node::node("f", {*a}));
  auto b = graph.add(Node::leaf("b"));
  auto fb = graph.add(Node::node("f", {b}));
  graph.merge(*a, b, {eggc::UnionKind::User, "assumption", {}});
  graph.rebuild();
  auto congruence = graph.explain_equivalence(fa, fb);
  check(congruence.size() == 1 &&
            congruence[0].justification.kind == eggc::UnionKind::Congruence &&
            congruence[0].justification.premises.size() == 1,
        "congruence provenance is missing");
  auto reverse = graph.explain_equivalence(fb, fa);
  check(reverse.front().lhs == fb && reverse.back().rhs == fa,
        "reverse explanation has wrong direction");
  auto foreign = graph.add(Node::leaf("foreign"));
  graph.rebuild();
  throws<std::invalid_argument>(
      [&] { graph.explain_equivalence(root, foreign); });
  graph.check_invariants();

  Graph hooked;
  auto x = hooked.add(Node::leaf("x"));
  std::vector<eggc::IterationHook<Node>> hooks;
  hooks.emplace_back([&](Graph &g, const eggc::RunReport &report) {
    check(g.is_clean(), "iteration hook saw a dirty graph");
    if (report.iterations == 1) {
      g.add(Node::leaf("extra"));
      return true;
    }
    return false;
  });
  auto report = eggc::run(hooked, std::vector<eggc::Rewrite<Node>>{},
                          eggc::RunOptions{}, hooks);
  check(report.reason == eggc::StopReason::UserRequested && hooked.is_clean() &&
            hooked.node_count() == 2,
        "hook mutation caused false saturation or stop left dirty graph");
  check(hooked.lookup(Node::leaf("x")) == x, "hook changed existing root");

  // Mutating the caller's rule/hook vectors cannot invalidate the run snapshot.
  Graph frozen;
  frozen.add(Node::leaf("a"));
  std::vector<eggc::Rewrite<Node>> mutable_rules{
      eggc::rewrite("a-b", "a", "b")};
  std::vector<eggc::IterationHook<Node>> mutable_hooks;
  mutable_hooks.emplace_back([&](Graph &, const eggc::RunReport &) {
    mutable_rules.clear();
    mutable_hooks.clear();
    return true;
  });
  auto snapshot =
      eggc::run(frozen, mutable_rules, eggc::RunOptions{}, mutable_hooks);
  check(snapshot.reason == eggc::StopReason::Saturated &&
            frozen.lookup(Node::leaf("b")),
        "run did not freeze mutable rules and hooks");
}
void extraction_costs_and_dags() {
  Graph graph;
  auto f = graph.add_expr(eggc::parse_expr("(f x x x)"));
  auto g = graph.add_expr(eggc::parse_expr("(g (g x))"));
  graph.merge(f, g);
  graph.rebuild();
  check(eggc::to_string(eggc::Extractor<Node>(graph).find_best(f).second) ==
            "(g (g x))",
        "tree extraction changed objective");
  auto dag = eggc::DagExtractor<Node>(graph).solve(f);
  check(dag.optimal && dag.cost == 2 && dag.expression.nodes.size() == 2 &&
            eggc::to_string(dag.expression) == "(f x x x)",
        "DAG extraction double-counted shared children");
  eggc::DagOptions limit;
  limit.state_limit = 0;
  auto limited = eggc::DagExtractor<Node>(graph).solve(f, limit);
  check(!limited.optimal && !limited.cost && limited.explored_states == 0,
        "bounded DAG search claimed optimality");
  limit.state_limit = 3;
  auto incumbent = eggc::DagExtractor<Node>(graph).solve(f, limit);
  check(!incumbent.optimal && incumbent.cost == 2,
        "bounded DAG search lost incumbent");

  eggc::CostPolicy<Node, double> latency =
      [](const Node &n,
         const std::vector<double> &children) -> std::optional<double> {
    double result = n.op == "f" ? 100 : 0.5;
    for (double cost : children)
      result += cost;
    return result;
  };
  auto floating =
      eggc::Extractor<Node, eggc::NoAnalysis<Node>, double>(graph, latency)
          .find_best(f);
  check(floating.first == 1.5 &&
            eggc::to_string(floating.second) == "(g (g x))",
        "floating-point extraction failed");
  using Pair = std::pair<std::size_t, std::size_t>;
  eggc::CostPolicy<Node, Pair> lex =
      [](const Node &,
         const std::vector<Pair> &children) -> std::optional<Pair> {
    Pair result{1, 1};
    for (auto c : children) {
      result.first += c.first;
      result.second += c.second;
    }
    return result;
  };
  check(eggc::Extractor<Node, eggc::NoAnalysis<Node>, Pair>(graph, lex)
                .best_cost(f) == Pair{3, 3},
        "lexicographic extraction failed");
  throws<std::invalid_argument>([&] {
    eggc::Extractor<Node, eggc::NoAnalysis<Node>, double> invalid(
        graph,
        [](const Node &, const std::vector<double> &) -> std::optional<double> {
          return std::numeric_limits<double>::quiet_NaN();
        });
  });
  throws<std::invalid_argument>([&] {
    eggc::DagExtractor<Node>(graph, [](const Node &) { return -1.0; }).solve(f);
  });
  auto cycle = graph.add(Node::node("cycle", {f}));
  graph.merge(cycle, f);
  graph.rebuild();
  auto finite = eggc::DagExtractor<Node>(graph).solve(f);
  check(finite.optimal && finite.cost == 2, "DAG extraction selected a cycle");
  auto stale = eggc::DagExtractor<Node>(graph);
  graph.add(Node::leaf("later"));
  throws<std::logic_error>([&] { stale.solve(f); });
}
// Enumerate assignments to every class, then independently walk the chosen
// root DAG. This oracle uses neither branch-and-bound nor production cycle
// checks.
void dag_oracle() {
  std::mt19937 rng(313);
  for (int trial = 0; trial < 30; ++trial) {
    Graph graph;
    std::vector<eggc::Id> ids;
    for (int i = 0; i < 3; ++i)
      ids.push_back(graph.add(Node::leaf("a" + std::to_string(i))));
    for (int i = 0; i < 4; ++i)
      ids.push_back(graph.add(
          Node::node("f" + std::to_string(i),
                     {ids[rng() % ids.size()], ids[rng() % ids.size()]})));
    for (int i = 0; i < 2; ++i)
      graph.merge(ids[3 + rng() % 4], ids[rng() % ids.size()]);
    graph.rebuild();
    graph.check_invariants();
    const auto classes = graph.classes();
    std::unordered_map<eggc::Id, std::size_t> selected;
    std::size_t best = std::numeric_limits<std::size_t>::max();
    std::function<void(std::size_t)> enumerate;
    enumerate = [&](std::size_t position) {
      if (position < classes.size()) {
        auto id = classes[position];
        for (std::size_t i = 0; i < graph.nodes(id).size(); ++i) {
          selected[id] = i;
          enumerate(position + 1);
        }
        return;
      }
      std::unordered_map<eggc::Id, int> colors;
      std::size_t count = 0;
      std::function<bool(eggc::Id)> visit;
      visit = [&](eggc::Id id) {
        id = graph.find(id);
        if (colors[id] == 1)
          return false;
        if (colors[id] == 2)
          return true;
        colors[id] = 1;
        ++count;
        for (auto child : graph.nodes(id)[selected[id]].args)
          if (!visit(child))
            return false;
        colors[id] = 2;
        return true;
      };
      if (visit(ids.back()))
        best = std::min(best, count);
    };
    enumerate(0);
    auto actual = eggc::DagExtractor<Node>(graph).solve(ids.back());
    check(actual.optimal && actual.cost == static_cast<double>(best) &&
              actual.expression.nodes.size() == best,
          "DAG extraction disagrees with assignment oracle");
  }
}
void multipatterns_and_dot() {
  Graph graph;
  auto fa = graph.add_expr(eggc::parse_expr("(f a a)"));
  graph.add_expr(eggc::parse_expr("(f a b)"));
  graph.add_expr(eggc::parse_expr("(g a a)"));
  graph.rebuild();
  eggc::MultiPattern<Node> multi({{"?f", eggc::parse_pattern("(f ?x ?y)")},
                                  {"?g", eggc::parse_pattern("(g ?x ?y)")}});
  auto matches = multi.match(graph);
  check(matches.size() == 1 && matches[0].at("?f") == fa,
        "multipattern did not join shared bindings");
  bool emitted = false;
  check(!multi.search(graph,
                      [&](const eggc::Substitution &) {
                        emitted = true;
                        return false;
                      }) &&
            emitted,
        "multipattern ignored cancellation");
  auto rule = eggc::multi_rewrite<Node>("joined", multi, "?f",
                                        eggc::parse_pattern("?x"));
  eggc::run(graph, std::vector{rule});
  check(eggc::to_string(eggc::Extractor<Node>(graph).find_best(fa).second) ==
            "a",
        "multipattern rewrite failed");
  throws<std::invalid_argument>([&] {
    eggc::multi_rewrite<Node>("bad", multi, "?missing",
                              eggc::parse_pattern("?x"));
  });
  graph.add(Node::leaf("quote\"\\\n"));
  graph.rebuild();
  auto dot = eggc::to_dot(graph);
  check(dot.starts_with("digraph egraph") &&
            dot.find("quote\\\"\\\\\\n") != std::string::npos,
        "DOT export did not escape labels");
  graph.check_invariants();
}
std::set<std::vector<std::pair<std::string, eggc::Id>>>
normalize(const std::vector<eggc::Substitution> &matches) {
  std::set<std::vector<std::pair<std::string, eggc::Id>>> result;
  for (const auto &subst : matches) {
    std::vector<std::pair<std::string, eggc::Id>> key(subst.begin(),
                                                      subst.end());
    std::sort(key.begin(), key.end());
    result.insert(std::move(key));
  }
  return result;
}
void compiled_matcher_oracle() {
  std::mt19937 rng(1123);
  for (int trial = 0; trial < 30; ++trial) {
    Graph graph;
    std::vector<eggc::Id> ids;
    for (int i = 0; i < 40; ++i) {
      Node node = Node::leaf("a" + std::to_string(rng() % 4));
      if (i > 4)
        node = Node::node(rng() % 2 ? "f" : "g",
                          {ids[rng() % ids.size()], ids[rng() % ids.size()]});
      ids.push_back(graph.add(node));
      if (i > 4 && rng() % 4 == 0)
        graph.merge(ids[rng() % ids.size()], ids.back());
      if (i % 7 == 0) {
        graph.rebuild();
        graph.check_invariants();
      }
    }
    graph.rebuild();
    graph.check_invariants();
    for (auto text : {"?x", "(f ?x ?x)", "(f (g ?x ?y) ?y)",
                      "(g (f ?x ?y) (f ?y ?x))", "(f a0 ?x)"}) {
      auto pattern = eggc::parse_pattern(text);
      eggc::CompiledPattern<Node> compiled(pattern);
      for (auto root : graph.classes()) {
        std::vector<eggc::Substitution> reference, actual;
        // Independent recursive enumerator retained as a regression oracle.
        eggc::pattern_detail::enumerate(
            graph, pattern, static_cast<eggc::Id>(pattern.nodes.size() - 1),
            root, {},
            [&](const eggc::Substitution &subst) {
              reference.push_back(subst);
              return true;
            },
            {});
        compiled.search(graph, root, [&](const std::vector<eggc::Id> &values) {
          actual.push_back(compiled.substitution(values));
          return true;
        });
        check(normalize(reference) == normalize(actual),
              "compiled matcher disagrees with recursive oracle");
      }
    }
  }
}
} // namespace
int main() {
  try {
    hook_exceptions();
    nonexact_matching();
    dag_oracle();
    incremental_components();
    analysis_hooks_and_limits();
    lookup_hooks_and_provenance();
    extraction_costs_and_dags();
    multipatterns_and_dot();
    compiled_matcher_oracle();
    std::cout << "Advanced engine checks passed\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
