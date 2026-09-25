#include "eggc/testing.hpp"
#include <stdexcept>
#include <unordered_map>

namespace eggc {
namespace testing {
void rebuild_full_scan(EGraph& graph) {
    if (graph.analysis_) throw std::logic_error("full-scan reference does not support analyses");
    bool changed;
    do {
        changed = false;
        std::unordered_map<ENode, Id, ENodeHash> fresh;
        for (Id old = 0; old < graph.classes_.size(); ++old) {
            const Id owner = graph.find(old);
            for (const auto& original : graph.classes_[old]) {
                ENode node = original;
                for (Id& child : node.children) child = graph.find(child);
                const auto inserted = fresh.emplace(node, owner);
                if (!inserted.second && graph.merge(owner, inserted.first->second)) changed = true;
            }
        }
    } while (changed);

    std::vector<std::vector<ENode>> compact(graph.classes_.size());
    std::unordered_map<ENode, Id, ENodeHash> fresh;
    for (Id old = 0; old < graph.classes_.size(); ++old) {
        const Id owner = graph.find(old);
        for (const auto& original : graph.classes_[old]) {
            ENode node = original;
            for (Id& child : node.children) child = graph.find(child);
            if (fresh.emplace(node, owner).second) compact[owner].push_back(std::move(node));
        }
    }
    graph.classes_.swap(compact);
    graph.stored_node_count_ = 0;
    graph.memo_.clear();
    graph.op_index_.clear();
    for (Id id = 0; id < graph.classes_.size(); ++id) {
        if (graph.find(id) != id) continue;
        graph.stored_node_count_ += graph.classes_[id].size();
        for (const auto& node : graph.classes_[id]) {
            graph.memo_.emplace(node, id);
            auto& candidates = graph.op_index_[{node.op, node.children.size()}];
            if (candidates.empty() || candidates.back() != id) candidates.push_back(id);
        }
    }
    graph.clean_ = true;
}
}
}
