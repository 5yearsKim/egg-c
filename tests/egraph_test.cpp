#include "eggc/testing.hpp"
#include <iostream>
#include <random>
#include <vector>

namespace {
bool check(bool value, const char *message) {
  if (value)
    return true;
  std::cerr << "FAILED: " << message << '\n';
  return false;
}

bool worklist_agrees_with_full_scan_reference() {
  eggc::EGraph worklist;
  eggc::EGraph reference;
  std::vector<eggc::Id> ids;
  std::mt19937 random(0xE66);
  auto add_leaf = [&](const std::string &op) {
    ids.push_back(worklist.add(op));
    reference.add(op);
  };
  auto add_node = [&](const std::string &op, eggc::Id left, eggc::Id right) {
    ids.push_back(worklist.add(op, {left, right}));
    reference.add(op, {left, right});
  };

  add_leaf("a");
  add_leaf("b");
  add_leaf("c");
  add_leaf("d");
  for (std::size_t i = 0; i < 250; ++i) {
    const auto left = ids[random() % ids.size()];
    const auto right = ids[random() % ids.size()];
    add_node(i % 2 == 0 ? "f" : "g", left, right);
    if (i % 3 == 0) {
      const auto a = ids[random() % ids.size()];
      const auto b = ids[random() % ids.size()];
      worklist.merge(a, b);
      reference.merge(a, b);
    }
  }
  for (std::size_t i = 0; i < 60; ++i) {
    const auto a = ids[random() % ids.size()];
    const auto b = ids[random() % ids.size()];
    worklist.merge(a, b);
    reference.merge(a, b);
  }

  worklist.rebuild();
  eggc::testing::rebuild_full_scan(reference);
  if (!check(worklist.node_count() == reference.node_count(),
             "reference node counts agree") ||
      !check(worklist.class_count() == reference.class_count(),
             "reference class counts agree"))
    return false;
  for (const auto a : ids)
    for (const auto b : ids)
      if (check((worklist.find(a) == worklist.find(b)) ==
                    (reference.find(a) == reference.find(b)),
                "reference equivalence relations agree") == false)
        return false;
  return true;
}
} // namespace

int main() { return worklist_agrees_with_full_scan_reference() ? 0 : 1; }
