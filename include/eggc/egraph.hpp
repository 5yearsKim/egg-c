#pragma once
#include "enode.hpp"
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace eggc {
class EGraph {
public:
    Id add(ENode node);
    Id add(const std::string& op, std::vector<Id> children = {});
    Id find(Id id);
    Id find(Id id) const;
    bool merge(Id a, Id b);
    void rebuild();
    const std::vector<ENode>& nodes(Id id) const;
    std::vector<Id> classes() const;
    std::size_t class_count() const;
    std::size_t node_count() const;
    std::uint64_t revision() const noexcept { return revision_; }

private:
    std::vector<Id> parent_;
    std::vector<unsigned> rank_;
    std::vector<std::vector<ENode>> classes_;
    std::unordered_map<ENode, Id, ENodeHash> memo_;
    std::size_t stored_node_count_ = 0;
    std::uint64_t revision_ = 0;
};
}
