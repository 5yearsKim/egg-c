#pragma once
#include <unordered_map>
namespace eggc {
template <Language L>
CompiledReplacement<L>::CompiledReplacement(Pattern<L> pattern,
                                            std::vector<std::string> variables)
    : pattern_(std::move(pattern)), variables_(std::move(variables)) {
  pattern_.validate();
  if (variables_.empty()) variables_ = pattern_.variables();
  std::unordered_map<std::string, Id> names;
  for (std::size_t i = 0; i < variables_.size(); ++i) {
    const auto& name = variables_[i];
    if (name.size() < 2 || name.front() != '?' ||
        !names.emplace(name, static_cast<Id>(i)).second)
      throw std::invalid_argument("invalid compiled variable list");
  }
  slots_.assign(pattern_.nodes.size(), invalid_id);
  for (std::size_t i = 0; i < pattern_.nodes.size(); ++i)
    if (auto var = std::get_if<Var>(&pattern_.nodes[i])) {
      auto found = names.find(var->name);
      if (found == names.end())
        throw std::invalid_argument("unbound compiled variable");
      slots_[i] = found->second;
    }
}
template <Language L>
Substitution CompiledReplacement<L>::substitution(
    const std::vector<Id>& bindings) const {
  if (bindings.size() != variables_.size())
    throw std::invalid_argument("invalid binding size");
  Substitution result;
  for (std::size_t i = 0; i < variables_.size(); ++i)
    if (bindings[i] != invalid_id) result.emplace(variables_[i], bindings[i]);
  return result;
}
template <Language L>
template <class A>
Id CompiledReplacement<L>::instantiate(EGraph<L, A>& graph,
                                       const std::vector<Id>& bindings) const {
  std::vector<Id> scratch;
  return instantiate(graph, bindings, scratch);
}
template <Language L>
template <class A>
Id CompiledReplacement<L>::instantiate(EGraph<L, A>& graph,
                                       const std::vector<Id>& bindings,
                                       std::vector<Id>& scratch) const {
  if (bindings.size() != variables_.size())
    throw std::invalid_argument("invalid binding size");
  for (auto slot : slots_)
    if (slot != invalid_id) graph.find(bindings[slot]);
  scratch.clear();
  scratch.reserve(pattern_.nodes.size());
  for (std::size_t i = 0; i < pattern_.nodes.size(); ++i) {
    if (slots_[i] != invalid_id)
      scratch.push_back(graph.find(bindings[slots_[i]]));
    else {
      L node = std::get<L>(pattern_.nodes[i]);
      for (Id& child : node.children_mut()) child = scratch[child];
      scratch.push_back(graph.add(std::move(node)));
    }
  }
  return scratch.back();
}
template <Language L>
template <class A>
std::optional<Id> CompiledReplacement<L>::lookup(
    const EGraph<L, A>& graph, const std::vector<Id>& bindings) const {
  graph.require_clean();
  if (bindings.size() != variables_.size())
    throw std::invalid_argument("invalid binding size");
  for (auto slot : slots_)
    if (slot != invalid_id) graph.find(bindings[slot]);
  std::vector<Id> ids;
  ids.reserve(pattern_.nodes.size());
  for (std::size_t i = 0; i < pattern_.nodes.size(); ++i) {
    if (slots_[i] != invalid_id)
      ids.push_back(graph.find(bindings[slots_[i]]));
    else {
      L node = std::get<L>(pattern_.nodes[i]);
      for (Id& child : node.children_mut()) child = ids[child];
      auto found = graph.lookup(std::move(node));
      if (!found) return {};
      ids.push_back(*found);
    }
  }
  return ids.back();
}
template <Language L, class A>
  requires AnalysisFor<A, L>
Id instantiate(EGraph<L, A>& graph, const Pattern<L>& pattern,
               const Substitution& subst) {
  const CompiledReplacement<L> program(pattern);
  std::vector<Id> bindings;
  bindings.reserve(program.variables().size());
  for (const auto& name : program.variables())
    bindings.push_back(subst.at(name));
  return program.instantiate(graph, bindings);
}
}  // namespace eggc
