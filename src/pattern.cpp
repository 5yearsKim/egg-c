#include "eggc/pattern.hpp"
#include <algorithm>
#include <iterator>
#include <set>
#include <stdexcept>
#include <utility>

namespace eggc {
Pattern Pattern::var(std::string name) { return Pattern{"?" + name, {}}; }
Pattern Pattern::node(std::string op, std::vector<Pattern> children) {
    return Pattern{std::move(op), std::move(children)};
}
bool Pattern::is_var() const { return !op.empty() && op[0] == '?'; }

namespace {
using MatchKey = std::vector<std::pair<std::string, Id>>;

MatchKey key_for(const EGraph& graph, const Substitution& subst) {
    MatchKey key;
    key.reserve(subst.size());
    for (const auto& binding : subst)
        key.emplace_back(binding.first, graph.find(binding.second));
    std::sort(key.begin(), key.end());
    return key;
}

std::vector<Substitution> match_all(const EGraph& graph, const Pattern& pattern,
                                   Id eclass, const Substitution& incoming) {
    eclass = graph.find(eclass);
    if (pattern.is_var()) {
        auto it = incoming.find(pattern.op);
        if (it != incoming.end()) {
            if (graph.find(it->second) == eclass) return {incoming};
            return {};
        }
        Substitution extended = incoming;
        extended.emplace(pattern.op, eclass);
        return {std::move(extended)};
    }

    std::vector<Substitution> results;
    for (const auto& node : graph.nodes(eclass)) {
        if (node.op != pattern.op || node.children.size() != pattern.children.size()) continue;

        std::vector<Substitution> partials{incoming};
        for (std::size_t i = 0; i < pattern.children.size() && !partials.empty(); ++i) {
            std::vector<Substitution> next;
            for (const auto& partial : partials) {
                auto child_matches = match_all(graph, pattern.children[i], node.children[i], partial);
                next.insert(next.end(), std::make_move_iterator(child_matches.begin()),
                            std::make_move_iterator(child_matches.end()));
            }
            partials = std::move(next);
        }
        results.insert(results.end(), std::make_move_iterator(partials.begin()),
                       std::make_move_iterator(partials.end()));
    }
    return results;
}
}
std::vector<Substitution> match(const EGraph& graph, const Pattern& pattern, Id eclass) {
    auto results = match_all(graph, pattern, eclass, {});
    std::set<MatchKey> seen;
    std::vector<Substitution> unique;
    unique.reserve(results.size());
    for (auto& subst : results) {
        for (auto& binding : subst) binding.second = graph.find(binding.second);
        if (seen.insert(key_for(graph, subst)).second)
            unique.push_back(std::move(subst));
    }
    return unique;
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
