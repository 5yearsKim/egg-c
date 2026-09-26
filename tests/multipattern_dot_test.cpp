
#include "support/check.hpp"

namespace {
using namespace test_support;
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
                      [&](const eggc::Substitution&) {
                        emitted = true;
                        return false;
                      }) &&
            emitted,
        "multipattern ignored cancellation");
  auto rule = eggc::multi_rewrite<Node>("joined", multi, "?f",
                                        eggc::parse_pattern("?x"));
  eggc::run(graph, std::vector{rule});
  check(
      eggc::to_string(eggc::Extractor<Node>(graph).find_best(fa).second) == "a",
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

}  // namespace
int main() { return test_support::run_tests(multipatterns_and_dot); }
