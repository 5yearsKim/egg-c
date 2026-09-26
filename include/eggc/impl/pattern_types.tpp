#pragma once
#include <algorithm>
#include <set>
#include <stdexcept>
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

}  // namespace eggc
