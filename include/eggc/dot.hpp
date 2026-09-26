#pragma once
#include <string>

#include "egraph.hpp"
#include "language_io.hpp"

namespace eggc {
namespace dot_detail {
inline std::string quote(const std::string &text) {
  std::string result = "\"";
  for (char c : text) {
    switch (c) {
    case '\\':
      result += "\\\\";
      break;
    case '"':
      result += "\\\"";
      break;
    case '\n':
      result += "\\n";
      break;
    case '\r':
      result += "\\r";
      break;
    case '\t':
      result += "\\t";
      break;
    default:
      result += c;
    }
  }
  return result + '"';
}
} // namespace dot_detail
template <Language L, class A, class Formatter>
std::string to_dot(const EGraph<L, A> &graph, Formatter format) {
  graph.require_clean();
  std::string result = "digraph egraph {\n  compound=true;\n";
  for (Id id : graph.classes()) {
    result += "  subgraph cluster_e" + std::to_string(id) +
              " {\n    label=\"e" + std::to_string(id) + "\";\n";
    result += "    e" + std::to_string(id) + " [shape=point];\n";
    std::size_t index = 0;
    for (const auto &node : graph.nodes(id)) {
      result += "    n" + std::to_string(id) + "_" + std::to_string(index++) +
                " [label=" + dot_detail::quote(format(node)) + "];\n";
    }
    result += "  }\n";
  }
  for (Id id : graph.classes()) {
    std::size_t index = 0;
    for (const auto &node : graph.nodes(id)) {
      std::size_t child_index = 0;
      for (Id child : node.children())
        result += "  n" + std::to_string(id) + "_" + std::to_string(index) +
                  " -> e" + std::to_string(graph.find(child)) + " [label=\"" +
                  std::to_string(child_index++) + "\"];\n";
      ++index;
    }
  }
  return result + "}\n";
}
template <PrintableLanguage L, class A>
std::string to_dot(const EGraph<L, A> &graph) {
  return to_dot(graph,
                [](const L &node) { return LanguageIO<L>::format_op(node); });
}
} // namespace eggc
