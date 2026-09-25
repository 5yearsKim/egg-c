#include "eggc/pattern.hpp"
#include <stdexcept>

namespace eggc {
Pattern Pattern::var(std::string name) { return Pattern{"?" + name, {}}; }
Pattern Pattern::node(std::string op, std::vector<Pattern> children) {
    return Pattern{std::move(op), std::move(children)};
}
bool Pattern::is_var() const { return !op.empty() && op[0] == '?'; }

namespace {
bool unify(const EGraph& g, const Pattern& p, Id id, Substitution& subst) {
    id = g.find(id);
    if (p.is_var()) {
        auto it = subst.find(p.op);
        if (it == subst.end()) { subst.emplace(p.op, id); return true; }
        return g.find(it->second) == id;
    }
    for (const auto& n : g.nodes(id)) {
        if (n.op != p.op || n.children.size() != p.children.size()) continue;
        Substitution next = subst;
        bool ok = true;
        for (std::size_t i = 0; i < p.children.size() && ok; ++i)
            ok = unify(g, p.children[i], n.children[i], next);
        if (ok) { subst = std::move(next); return true; }
    }
    return false;
}
}
std::vector<Substitution> match(const EGraph& graph, const Pattern& pattern, Id eclass) {
    Substitution subst;
    if (unify(graph, pattern, eclass, subst)) return {std::move(subst)};
    return {};
}
Id instantiate(EGraph& graph, const Pattern& pattern, const Substitution& subst) {
    if (pattern.is_var()) {
        auto it = subst.find(pattern.op);
        if (it == subst.end()) throw std::invalid_argument("unbound variable in rewrite rhs: " + pattern.op);
        return it->second;
    }
    std::vector<Id> children;
    children.reserve(pattern.children.size());
    for (const auto& child : pattern.children) children.push_back(instantiate(graph, child, subst));
    return graph.add(pattern.op, std::move(children));
}
}
