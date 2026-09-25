#pragma once
#include "id.hpp"
#include <functional>
#include <string>
#include <vector>

namespace eggc {
struct ENode {
    std::string op;
    std::vector<Id> children;
    bool operator==(const ENode& rhs) const { return op == rhs.op && children == rhs.children; }
};

struct ENodeHash {
    std::size_t operator()(const ENode& n) const noexcept {
        std::size_t h = std::hash<std::string>{}(n.op);
        for (Id id : n.children) {
            const std::size_t x = std::hash<Id>{}(id);
            h ^= x + 0x9e3779b9u + (h << 6) + (h >> 2);
        }
        return h;
    }
};
}
