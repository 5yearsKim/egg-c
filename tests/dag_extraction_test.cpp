#include <limits>
#include <random>

#include "support/check.hpp"

namespace {
using namespace test_support;
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
        if (colors[id] == 1) return false;
        if (colors[id] == 2) return true;
        colors[id] = 1;
        ++count;
        for (auto child : graph.nodes(id)[selected[id]].args)
          if (!visit(child)) return false;
        colors[id] = 2;
        return true;
      };
      if (visit(ids.back())) best = std::min(best, count);
    };
    enumerate(0);
    auto actual = eggc::DagExtractor<Node>(graph).solve(ids.back());
    check(actual.optimal && actual.cost == static_cast<double>(best) &&
              actual.expression.nodes.size() == best,
          "DAG extraction disagrees with assignment oracle");
  }
}

}  // namespace
int main() { return test_support::run_tests(dag_oracle); }
