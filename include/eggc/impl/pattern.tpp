#pragma once
#include <algorithm>
#include <set>
#include <stdexcept>

#include "matcher.hpp"

namespace eggc {
template <Language L>
Pattern<L> Pattern<L>::var(std::string name) {
  if (name.empty()) throw std::invalid_argument("empty pattern variable");
  if (name.front() != '?') name.insert(name.begin(), '?');
  if (name.size() == 1) throw std::invalid_argument("empty pattern variable");
  return Pattern{{Var{std::move(name)}}};
}
template <Language L>
Pattern<L> Pattern<L>::node(L prototype, std::vector<Pattern> children) {
  if (prototype.children().size() != children.size())
    throw std::invalid_argument("pattern prototype arity mismatch");
  Pattern result;
  for (std::size_t i = 0; i < children.size(); ++i) {
    const auto& child = children[i];
    child.validate();
    const auto offset = result.nodes.size();
    if (offset + child.nodes.size() >= invalid_id)
      throw std::overflow_error("pattern too large");
    for (auto entry : child.nodes) {
      if (auto* node = std::get_if<L>(&entry))
        for (Id& id : node->children_mut()) id += static_cast<Id>(offset);
      result.nodes.push_back(std::move(entry));
    }
    prototype.children_mut()[i] = static_cast<Id>(result.nodes.size() - 1);
  }
  result.nodes.push_back(std::move(prototype));
  return result;
}
template <Language L>
void Pattern<L>::validate() const {
  if (nodes.empty()) throw std::invalid_argument("empty pattern");
  if (nodes.size() >= invalid_id)
    throw std::overflow_error("pattern too large");
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    if (const auto* var = std::get_if<Var>(&nodes[i])) {
      if (var->name.size() < 2 || var->name.front() != '?')
        throw std::invalid_argument("invalid pattern variable");
    } else {
      for (Id child : std::get<L>(nodes[i]).children())
        if (child >= i)
          throw std::invalid_argument("pattern children must precede parent");
    }
  }
  // Earlier-child ordering lets a reverse pass visit the entire root DAG.
  // Disconnected variables cannot be bound by the root's matcher.
  std::vector<unsigned char> reachable(nodes.size(), 0);
  reachable.back() = 1;
  for (std::size_t i = nodes.size(); i-- > 0;) {
    if (!reachable[i])
      throw std::invalid_argument("pattern contains an unreachable entry");
    if (const auto* node = std::get_if<L>(&nodes[i]))
      for (Id child : node->children()) reachable[child] = 1;
  }
}
template <Language L>
std::vector<std::string> Pattern<L>::variables() const {
  std::set<std::string> names;
  for (const auto& entry : nodes)
    if (const auto* var = std::get_if<Var>(&entry)) names.insert(var->name);
  return {names.begin(), names.end()};
}

namespace pattern_detail {
template <Language L, class A>
  requires AnalysisFor<A, L>
bool enumerate(const EGraph<L, A>& graph, const Pattern<L>& pattern, Id entry,
               Id eclass, const Substitution& subst,
               const std::function<bool(const Substitution&)>& emit,
               const StopCheck& stop) {
  if (stop && stop()) return false;
  eclass = graph.find(eclass);
  if (const auto* var = std::get_if<Var>(&pattern.nodes[entry])) {
    const auto found = subst.find(var->name);
    if (found != subst.end())
      return graph.find(found->second) != eclass || emit(subst);
    auto bound = subst;
    bound.emplace(var->name, eclass);
    return emit(bound);
  }
  const auto& prototype = std::get<L>(pattern.nodes[entry]);
  for (const L& node : graph.nodes(eclass)) {
    if (stop && stop()) return false;
    if (!prototype.matches(node)) continue;
    // Defensive arity check even if a language's matches() omits it.
    if (prototype.children().size() != node.children().size()) continue;
    std::function<bool(std::size_t, const Substitution&)> visit_children;
    visit_children = [&](std::size_t i, const Substitution& current) {
      if (i == node.children().size()) return emit(current);
      return enumerate(
          graph, pattern, prototype.children()[i], node.children()[i], current,
          [&](const Substitution& next) { return visit_children(i + 1, next); },
          stop);
    };
    if (!visit_children(0, subst)) return false;
  }
  return true;
}
}  // namespace pattern_detail

template <Language L, class A, class Callback>
  requires AnalysisFor<A, L>
bool search_matches(const EGraph<L, A>& graph, const Pattern<L>& pattern,
                    Id eclass, Callback&& on_match, const StopCheck& stop) {
  const CompiledPattern<L> compiled(pattern);
  return compiled.search(graph, eclass, [&](const std::vector<Id>& bindings) {
    return on_match(compiled.substitution(bindings));
  }, stop);
}
template <Language L, class A>
  requires AnalysisFor<A, L>
std::vector<Substitution> match(const EGraph<L, A>& graph,
                                const Pattern<L>& pattern, Id eclass) {
  std::vector<Substitution> result;
  search_matches(graph, pattern, eclass, [&](const Substitution& subst) {
    result.push_back(subst);
    return true;
  });
  return result;
}
template <Language L, class A>
  requires AnalysisFor<A, L>
Id instantiate(EGraph<L, A>& graph, const Pattern<L>& pattern,
               const Substitution& subst) {
  pattern.validate();
  // Resolve all variables before adding any nodes.
  for (const auto& name : pattern.variables()) graph.find(subst.at(name));
  std::vector<Id> ids;
  for (const auto& entry : pattern.nodes) {
    if (const auto* var = std::get_if<Var>(&entry)) {
      ids.push_back(graph.find(subst.at(var->name)));
    } else {
      L node = std::get<L>(entry);
      for (Id& child : node.children_mut()) child = ids[child];
      ids.push_back(graph.add(std::move(node)));
    }
  }
  return ids.back();
}
}  // namespace eggc
