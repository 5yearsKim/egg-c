#pragma once
#include "egraph.hpp"
#include <unordered_map>

namespace eggc {
struct Pattern {
    std::string op;
    std::vector<Pattern> children;
    static Pattern var(std::string name);
    static Pattern node(std::string op, std::vector<Pattern> children = {});
    bool is_var() const;
};
using Substitution = std::unordered_map<std::string, Id>;
// Match against a rebuilt graph. Returns every distinct binding of pattern
// variables to canonical e-class IDs; result ordering is unspecified.
std::vector<Substitution> match(const EGraph& graph, const Pattern& pattern, Id eclass);
Id instantiate(EGraph& graph, const Pattern& pattern, const Substitution& subst);
}
