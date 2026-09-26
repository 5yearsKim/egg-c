#include <random>
#include <set>

#include "support/check.hpp"
#include "support/reference_matcher.hpp"
namespace {
using namespace test_support;
std::set<std::vector<std::pair<std::string, eggc::Id>>> test_normalize(
    const std::vector<eggc::Substitution>& matches) {
  std::set<std::vector<std::pair<std::string, eggc::Id>>> result;
  for (const auto& subst : matches) {
    std::vector<std::pair<std::string, eggc::Id>> key(subst.begin(),
                                                      subst.end());
    std::sort(key.begin(), key.end());
    result.insert(std::move(key));
  }
  return result;
}

struct LooseNode : Node {
  static constexpr bool exact_matches = false;
  using Discriminant = std::size_t;
  Discriminant discriminant() const { return args.size(); }
  bool matches(const LooseNode& other) const {
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
        test_support::reference::enumerate(
            graph, pattern, static_cast<eggc::Id>(pattern.nodes.size() - 1),
            root, {},
            [&](const eggc::Substitution& subst) {
              reference.push_back(subst);
              return true;
            },
            {});
        compiled.search(graph, root, [&](const std::vector<eggc::Id>& values) {
          actual.push_back(compiled.substitution(values));
          return true;
        });
        check(test_normalize(reference) == test_normalize(actual),
              "compiled matcher disagrees with recursive oracle");
      }
    }
  }
}

}  // namespace
int main() {
  return test_support::run_tests(nonexact_matching, compiled_matcher_oracle);
}
