#include "eggc/extract.hpp"
#include <limits>
#include <sstream>
#include <stdexcept>

namespace eggc {
namespace {
struct Choice { bool known = false; ENode node; };
}
Expr extract(const EGraph& graph, Id root) {
    const auto ids = graph.classes();
    std::vector<std::size_t> costs;
    Id max_id = 0;
    for (Id id : ids) if (id > max_id) max_id = id;
    costs.assign(static_cast<std::size_t>(max_id) + 1, std::numeric_limits<std::size_t>::max());
    std::vector<Choice> choices(costs.size());
    bool changed = true;
    while (changed) {
        changed = false;
        for (Id id : ids) {
            for (const auto& node : graph.nodes(id)) {
                std::size_t cost = 1;
                bool finite = true;
                for (Id child : node.children) {
                    child = graph.find(child);
                    if (child >= costs.size() || costs[child] == std::numeric_limits<std::size_t>::max() ||
                        cost > std::numeric_limits<std::size_t>::max() - costs[child]) { finite = false; break; }
                    cost += costs[child];
                }
                if (finite && cost < costs[id]) {
                    costs[id] = cost; choices[id] = {true, node}; changed = true;
                }
            }
        }
    }
    root = graph.find(root);
    if (root >= choices.size() || !choices[root].known)
        throw std::runtime_error("no finite expression represented by e-class");
    const ENode& node = choices[root].node;
    Expr result{node.op, {}};
    for (Id child : node.children) result.children.push_back(extract(graph, child));
    return result;
}
std::string to_string(const Expr& expr) {
    if (expr.children.empty()) return expr.op;
    std::ostringstream out;
    out << '(' << expr.op;
    for (const auto& child : expr.children) out << ' ' << to_string(child);
    return out.str() + ')';
}
}
