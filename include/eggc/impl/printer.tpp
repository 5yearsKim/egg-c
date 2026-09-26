// Included by printer.hpp.
#pragma once
#include <variant>
#include <vector>

#include "sexpr.hpp"

namespace eggc {
namespace text_detail {
template <PrintableLanguage L>
std::string operator_text(const L& node) {
  return quote(LanguageIO<L>::format_op(node));
}

template <PrintableLanguage L, class Canonicalize>
std::string print_node(const L& node, Canonicalize canonicalize) {
  const auto& children = node.children();
  std::string result;
  if (children.size() != 0) result += '(';
  result += operator_text(node);
  for (Id child : children) {
    result += " e";
    result += std::to_string(canonicalize(child));
  }
  if (children.size() != 0) result += ')';
  return result;
}

// Visit only the root-reachable expression, unfolding DAG sharing as text.
// A stack avoids recursion and repeated construction of subtree strings.
template <class Entries, class GetNode, class GetVariable>
std::string print(const Entries& entries, GetNode get_node,
                  GetVariable get_variable) {
  if (entries.empty())
    throw std::invalid_argument("cannot print an empty expression");
  for (std::size_t i = 0; i < entries.size(); ++i)
    if (const auto* node = get_node(entries[i]))
      for (Id child : node->children())
        if (child >= i)
          throw std::invalid_argument("children must precede parent");
  struct Visit {
    std::size_t id;
    std::size_t next_child;
    bool started;
  };
  std::vector<Visit> stack{{entries.size() - 1, 0, false}};
  std::string result;
  while (!stack.empty()) {
    auto& visit = stack.back();
    const auto& entry = entries[visit.id];
    if (const auto* variable = get_variable(entry)) {
      for (char c : variable->name)
        if (whitespace(c) || c == '(' || c == ')' || c == '"' || c == '\\')
          throw std::invalid_argument("pattern variable requires a bare token");
      result += variable->name;
      stack.pop_back();
      continue;
    }
    const auto& node = *get_node(entry);
    const auto& children = node.children();
    if (!visit.started) {
      if (children.size() != 0) result += '(';
      result += operator_text(node);
      visit.started = true;
    }
    if (visit.next_child < children.size()) {
      const auto child = children[visit.next_child++];
      result += ' ';
      stack.push_back({child, 0, false});
    } else {
      if (children.size() != 0) result += ')';
      stack.pop_back();
    }
  }
  return result;
}
}  // namespace text_detail

template <PrintableLanguage L>
std::string to_string(const L& node) {
  return text_detail::print_node(node, [](Id id) { return id; });
}

template <PrintableLanguage L, class A>
std::string to_string(const EGraph<L, A>& graph) {
  graph.require_clean();
  std::string result;
  for (Id id : graph.classes()) {
    result += "e" + std::to_string(id) + ":\n";
    for (const auto& node : graph.nodes(id)) {
      result += "  ";
      result += text_detail::print_node(
          node, [&graph](Id child) { return graph.find(child); });
      result += '\n';
    }
  }
  return result;
}

template <PrintableLanguage L>
std::string to_string(const RecExpr<L>& expr) {
  return text_detail::print(
      expr.nodes, [](const L& node) { return &node; },
      [](const L&) -> const Var* { return nullptr; });
}
template <PrintableLanguage L>
std::string to_string(const Pattern<L>& pattern) {
  pattern.validate();
  return text_detail::print(
      pattern.nodes, [](const auto& entry) { return std::get_if<L>(&entry); },
      [](const auto& entry) { return std::get_if<Var>(&entry); });
}
}  // namespace eggc
