#pragma once
#include <algorithm>
#include <deque>
#include <stdexcept>

#include "../explain.hpp"
#include "union_find.hpp"
namespace eggc::detail {
class ProofForest {
 public:
  const std::vector<ProofStep>& steps() const noexcept { return steps_; }
  void add(ProofStep step, std::size_t slots) {
    edges_.resize(slots);
    const auto index = steps_.size();
    edges_[step.lhs].push_back({step.rhs, index});
    edges_[step.rhs].push_back({step.lhs, index});
    steps_.push_back(std::move(step));
  }
  std::vector<ProofStep> path(Id lhs, Id rhs, std::size_t slots) const {
    if (lhs == rhs) return {};
    std::vector<Id> previous(slots, invalid_id);
    std::vector<std::size_t> indexes(slots);
    std::deque<Id> queue{lhs};
    previous[lhs] = lhs;
    while (!queue.empty() && previous[rhs] == invalid_id) {
      auto id = queue.front();
      queue.pop_front();
      if (id >= edges_.size()) continue;
      for (auto [next, index] : edges_[id])
        if (previous[next] == invalid_id) {
          previous[next] = id;
          indexes[next] = index;
          queue.push_back(next);
        }
    }
    if (previous[rhs] == invalid_id)
      throw std::logic_error("incomplete equality provenance");
    std::vector<ProofStep> result;
    for (Id id = rhs; id != lhs; id = previous[id]) {
      auto step = steps_[indexes[id]];
      if (step.lhs != previous[id]) std::swap(step.lhs, step.rhs);
      result.push_back(std::move(step));
    }
    std::reverse(result.begin(), result.end());
    return result;
  }
  // Replay unions independently. Every semantic premise must be accepted by
  // the caller's checker; congruence child equalities must already be proven.
  template <class Validator>
  bool verify(std::size_t slots, const Validator& validate) const {
    UnionFind replay;
    for (std::size_t i = 0; i < slots; ++i) replay.add();
    for (const auto& step : steps_) {
      for (auto [a, b] : step.justification.premises)
        if (a >= slots || b >= slots || replay.find(a) != replay.find(b))
          return false;
      if (!validate(step)) return false;
      auto a = replay.find(step.lhs), b = replay.find(step.rhs);
      if (a == b) return false;
      auto ordered = replay.order(a, b);
      replay.link(ordered.first, ordered.second);
    }
    return true;
  }
  std::size_t storage_bytes() const {
    auto total =
        steps_.capacity() * sizeof(ProofStep) +
        edges_.capacity() * sizeof(std::vector<std::pair<Id, std::size_t>>);
    for (const auto& step : steps_)
      total +=
          step.justification.premises.capacity() * sizeof(std::pair<Id, Id>) +
          step.justification.name.capacity();
    for (const auto& edges : edges_)
      total += edges.capacity() * sizeof(std::pair<Id, std::size_t>);
    return total;
  }

 private:
  std::vector<ProofStep> steps_;
  std::vector<std::vector<std::pair<Id, std::size_t>>> edges_;
};
}  // namespace eggc::detail
