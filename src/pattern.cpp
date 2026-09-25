#include "eggc/pattern.hpp"
#include <algorithm>
#include <functional>
#include <iterator>
#include <set>
#include <stdexcept>
#include <utility>

namespace eggc {
Pattern Pattern::var(std::string name) {
  return Pattern{"?" + name, {}, Kind::Variable};
}
Pattern Pattern::node(std::string op, std::vector<Pattern> children) {
  return Pattern{std::move(op), std::move(children), Kind::Node};
}
bool Pattern::is_var() const { return kind == Kind::Variable; }

namespace {
using MatchKey = std::vector<std::pair<std::string, Id>>;

MatchKey key_for(const EGraph &graph, const Substitution &subst) {
  MatchKey key;
  key.reserve(subst.size());
  for (const auto &binding : subst)
    key.emplace_back(binding.first, graph.find(binding.second));
  std::sort(key.begin(), key.end());
  return key;
}

bool enumerate(const EGraph &graph, const Pattern &pattern, Id eclass,
               const Substitution &incoming,
               const std::function<bool(const Substitution &)> &emit,
               const std::function<bool()> &should_stop) {
  if (should_stop && should_stop())
    return false;
  eclass = graph.find(eclass);
  if (pattern.is_var()) {
    auto it = incoming.find(pattern.op);
    if (it != incoming.end())
      return graph.find(it->second) != eclass || emit(incoming);
    Substitution extended = incoming;
    extended.emplace(pattern.op, eclass);
    return emit(extended);
  }

  for (const auto &node : graph.nodes(eclass)) {
    if (should_stop && should_stop())
      return false;
    if (node.op != pattern.op ||
        node.children.size() != pattern.children.size())
      continue;

    std::function<bool(std::size_t, const Substitution &)> match_children;
    match_children = [&](std::size_t child, const Substitution &subst) {
      if (should_stop && should_stop())
        return false;
      if (child == pattern.children.size())
        return emit(subst);
      return enumerate(
          graph, pattern.children[child], node.children[child], subst,
          [&](const Substitution &next) {
            return match_children(child + 1, next);
          },
          should_stop);
    };
    if (!match_children(0, incoming))
      return false;
  }
  return true;
}
} // namespace
std::vector<Substitution> match(const EGraph &graph, const Pattern &pattern,
                                Id eclass) {
  std::vector<Substitution> results;
  search_matches(graph, pattern, eclass, [&](const Substitution &subst) {
    results.push_back(subst);
    return true;
  });
  return results;
}
bool search_matches(const EGraph &graph, const Pattern &pattern, Id eclass,
                    const std::function<bool(const Substitution &)> &on_match,
                    const std::function<bool()> &should_stop) {
  graph.require_clean();
  std::set<MatchKey> seen;
  return enumerate(
      graph, pattern, eclass, {},
      [&](const Substitution &found) {
        Substitution canonical = found;
        for (auto &binding : canonical)
          binding.second = graph.find(binding.second);
        if (!seen.insert(key_for(graph, canonical)).second)
          return true;
        return on_match(canonical);
      },
      should_stop);
}
Id instantiate(EGraph &graph, const Pattern &pattern,
               const Substitution &subst) {
  if (pattern.is_var()) {
    auto it = subst.find(pattern.op);
    if (it == subst.end())
      throw std::invalid_argument("unbound variable in rewrite rhs: " +
                                  pattern.op);
    return it->second;
  }
  std::vector<Id> children;
  children.reserve(pattern.children.size());
  for (const auto &child : pattern.children)
    children.push_back(instantiate(graph, child, subst));
  return graph.add(pattern.op, std::move(children));
}
} // namespace eggc
