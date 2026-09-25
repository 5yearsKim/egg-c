#include "eggc/egraph.hpp"
#include <algorithm>
#include <stdexcept>

namespace eggc {
Id EGraph::find(Id id) {
    if (id >= parent_.size()) throw std::out_of_range("invalid e-class id");
    if (parent_[id] != id) parent_[id] = find(parent_[id]);
    return parent_[id];
}
Id EGraph::find(Id id) const {
    if (id >= parent_.size()) throw std::out_of_range("invalid e-class id");
    while (parent_[id] != id) id = parent_[id];
    return id;
}
Id EGraph::add(ENode node) {
    for (Id& child : node.children) child = find(child);
    auto it = memo_.find(node);
    if (it != memo_.end()) return find(it->second);
    if (parent_.size() >= invalid_id) throw std::overflow_error("too many e-classes");
    Id id = static_cast<Id>(parent_.size());
    parent_.push_back(id); rank_.push_back(0); classes_.push_back({node});
    memo_.emplace(std::move(node), id);
    return id;
}
Id EGraph::add(const std::string& op, std::vector<Id> children) {
    return add(ENode{op, std::move(children)});
}
bool EGraph::merge(Id a, Id b) {
    a = find(a); b = find(b);
    if (a == b) return false;
    if (rank_[a] < rank_[b]) std::swap(a, b);
    parent_[b] = a;
    if (rank_[a] == rank_[b]) ++rank_[a];
    return true;
}
void EGraph::rebuild() {
    bool changed;
    do {
        changed = false;
        std::unordered_map<ENode, Id, ENodeHash> fresh;
        for (Id old = 0; old < classes_.size(); ++old) {
            Id owner = find(old);
            for (const auto& original : classes_[old]) {
                ENode node = original;
                for (Id& child : node.children) child = find(child);
                auto inserted = fresh.emplace(node, owner);
                if (!inserted.second && merge(owner, inserted.first->second)) changed = true;
            }
        }
    } while (changed);

    std::vector<std::vector<ENode>> compact(classes_.size());
    std::unordered_map<ENode, Id, ENodeHash> fresh;
    for (Id old = 0; old < classes_.size(); ++old) {
        Id owner = find(old);
        for (const auto& original : classes_[old]) {
            ENode node = original;
            for (Id& child : node.children) child = find(child);
            if (fresh.emplace(node, owner).second) compact[owner].push_back(std::move(node));
        }
    }
    classes_.swap(compact);
    memo_.clear();
    for (Id id = 0; id < classes_.size(); ++id)
        if (find(id) == id) for (const auto& node : classes_[id]) memo_.emplace(node, id);
}
const std::vector<ENode>& EGraph::nodes(Id id) const { return classes_.at(find(id)); }
std::vector<Id> EGraph::classes() const {
    std::vector<Id> result;
    for (Id i = 0; i < parent_.size(); ++i) if (find(i) == i) result.push_back(i);
    return result;
}
std::size_t EGraph::class_count() const { return classes().size(); }
std::size_t EGraph::node_count() const {
    std::size_t n = 0;
    for (Id id : classes()) n += classes_[id].size();
    return n;
}
}
