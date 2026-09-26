#pragma once
#include <stdexcept>
#include <utility>
#include <vector>

#include "../id.hpp"
namespace eggc::detail {
class UnionFind {
 public:
  std::size_t size() const noexcept { return parent_.size(); }
  bool empty() const noexcept { return parent_.empty(); }
  void add() {
    parent_.push_back(static_cast<Id>(parent_.size()));
    rank_.push_back(0);
  }
  Id find(Id id) {
    Id root = static_cast<const UnionFind&>(*this).find(id);
    while (parent_[id] != id) {
      Id next = parent_[id];
      parent_[id] = root;
      id = next;
    }
    return root;
  }
  Id find(Id id) const {
    if (id >= size()) throw std::out_of_range("invalid e-class id");
    while (parent_[id] != id) id = parent_[id];
    return id;
  }
  std::pair<Id, Id> order(Id a, Id b) const {
    if (rank_[a] < rank_[b]) std::swap(a, b);
    return {a, b};
  }
  void link(Id a, Id b) {
    parent_[b] = a;
    if (rank_[a] == rank_[b]) ++rank_[a];
  }
  std::size_t storage_bytes() const noexcept {
    return parent_.capacity() * sizeof(Id) +
           rank_.capacity() * sizeof(unsigned);
  }

 private:
  std::vector<Id> parent_;
  std::vector<unsigned> rank_;
};
}  // namespace eggc::detail
