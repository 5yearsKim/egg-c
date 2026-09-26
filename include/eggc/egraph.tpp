// Included by egraph.hpp.
#pragma once

#include <algorithm>
#include <deque>
#include <stdexcept>

namespace eggc {
template <Language L, class A>
void EGraph<L, A>::require_clean() const {
  if (!clean_) throw std::logic_error("query requires a rebuilt e-graph");
}

template <Language L, class A>
const typename A::Data& EGraph<L, A>::analysis_data(Id id) const {
  return analysis_data_.at(find(id));
}

template <Language L, class A>
Id EGraph<L, A>::find(Id id) {
  if (id >= parent_.size()) throw std::out_of_range("invalid e-class id");
  if (parent_[id] != id) parent_[id] = find(parent_[id]);
  return parent_[id];
}
template <Language L, class A>
Id EGraph<L, A>::find(Id id) const {
  if (id >= parent_.size()) throw std::out_of_range("invalid e-class id");
  while (parent_[id] != id) id = parent_[id];
  return id;
}
template <Language L, class A>
Id EGraph<L, A>::add(L node) {
  for (Id& child : node.children_mut()) child = find(child);
  auto it = memo_.find(node);
  if (it != memo_.end()) return find(it->second);
  if (parent_.size() >= invalid_id)
    throw std::overflow_error("too many e-classes");
  auto data = analysis_.make(*this, node);
  Id id = static_cast<Id>(parent_.size());
  parent_.push_back(id);
  rank_.push_back(0);
  classes_.push_back({node});
  analysis_data_.push_back(std::move(data));
  memo_.emplace(std::move(node), id);
  ++stored_node_count_;
  ++revision_;
  ++analysis_revision_;
  clean_ = false;
  return id;
}
template <Language L, class A>
Id EGraph<L, A>::add_expr(const RecExpr<L>& expr) {
  if (expr.nodes.empty())
    throw std::invalid_argument("cannot add an empty expression");
  for (std::size_t i = 0; i < expr.nodes.size(); ++i)
    for (Id child : expr.nodes[i].children())
      if (child >= i)
        throw std::invalid_argument("expression children must precede parent");
  std::vector<Id> ids;
  for (L node : expr.nodes) {
    for (Id& child : node.children_mut()) child = ids[child];
    ids.push_back(add(std::move(node)));
  }
  return ids.back();
}
template <Language L, class A>
bool EGraph<L, A>::merge(Id a, Id b) {
  a = find(a);
  b = find(b);
  if (a == b) return false;
  auto merged_data = analysis_data_[a];
  const auto result = analysis_.merge(merged_data, analysis_data_[b]);
  if (result == AnalysisMerge::Conflict)
    throw AnalysisConflict("e-class analysis conflict while merging classes");
  const bool analysis_changed = result == AnalysisMerge::Changed;
  if (rank_[a] < rank_[b]) std::swap(a, b);
  analysis_data_[a] = std::move(merged_data);
  parent_[b] = a;
  if (rank_[a] == rank_[b]) ++rank_[a];
  ++revision_;
  if (analysis_changed) {
    ++analysis_revision_;
    ++revision_;
  }
  clean_ = false;
  return true;
}
template <Language L, class A>
void EGraph<L, A>::close_congruence() {
  struct NodeUse {
    Id owner;
    std::size_t index;
  };
  std::unordered_map<L, Id, NodeHash<L>> fresh;
  std::vector<std::vector<NodeUse>> uses(classes_.size());
  std::vector<std::vector<unsigned char>> queued(classes_.size());
  for (std::size_t owner = 0; owner < classes_.size(); ++owner)
    queued[owner].resize(classes_[owner].size(), 0);
  std::deque<NodeUse> worklist;

  const auto enqueue = [&](NodeUse use) {
    if (!queued[use.owner][use.index]) {
      queued[use.owner][use.index] = 1;
      worklist.push_back(use);
    }
  };
  const auto enqueue_uses = [&](Id a, Id b) {
    for (const auto use : uses[a]) enqueue(use);
    for (const auto use : uses[b]) enqueue(use);
  };

  std::vector<std::pair<Id, Id>> initial_collisions;
  for (Id old = 0; old < classes_.size(); ++old) {
    const Id owner = find(old);
    for (std::size_t index = 0; index < classes_[old].size(); ++index) {
      auto& node = classes_[old][index];
      for (Id& child : node.children_mut()) {
        child = find(child);
        uses[child].push_back({old, index});
      }
      const auto inserted = fresh.emplace(node, owner);
      if (!inserted.second) {
        const Id other = find(inserted.first->second);
        if (find(owner) != other) initial_collisions.emplace_back(owner, other);
      }
    }
  }

  const auto merge_and_schedule = [&](Id lhs, Id rhs) {
    lhs = find(lhs);
    rhs = find(rhs);
    if (lhs == rhs) return;
    enqueue_uses(lhs, rhs);
    merge(lhs, rhs);
    const Id root = find(lhs);
    const Id dead = root == lhs ? rhs : lhs;
    uses[root].insert(uses[root].end(), uses[dead].begin(), uses[dead].end());
    uses[dead].clear();
  };

  for (const auto collision : initial_collisions)
    merge_and_schedule(collision.first, collision.second);

  while (!worklist.empty()) {
    const auto use = worklist.front();
    worklist.pop_front();
    queued[use.owner][use.index] = 0;
    auto& node = classes_[use.owner][use.index];
    for (Id& child : node.children_mut()) {
      child = find(child);
      uses[child].push_back(use);
    }
    const Id owner = find(use.owner);
    const auto inserted = fresh.emplace(node, owner);
    if (!inserted.second) {
      const Id other = find(inserted.first->second);
      if (owner != other) merge_and_schedule(owner, other);
    }
  }
}

template <Language L, class A>
bool EGraph<L, A>::propagate_analysis() {
  bool changed = false;
  for (Id old = 0; old < classes_.size(); ++old) {
    const Id id = find(old);
    for (const auto& node : classes_[old]) {
      const auto inferred = analysis_.make(*this, node);
      typename A::Data merged_data = analysis_data_[id];
      const auto result = analysis_.merge(merged_data, inferred);
      if (result == AnalysisMerge::Conflict)
        throw AnalysisConflict("e-class analysis conflict while rebuilding");
      if (result == AnalysisMerge::Changed) {
        analysis_data_[id] = std::move(merged_data);
        ++analysis_revision_;
        ++revision_;
        changed = true;
      }
    }
  }
  return changed;
}

template <Language L, class A>
bool EGraph<L, A>::compact_nodes() {
  bool changed = false;
  std::vector<std::vector<L>> compact(classes_.size());
  std::unordered_map<L, Id, NodeHash<L>> compact_memo;
  for (Id old = 0; old < classes_.size(); ++old) {
    const Id owner = find(old);
    for (const auto& original : classes_[old]) {
      L node = original;
      for (Id& child : node.children_mut()) child = find(child);
      const auto inserted = compact_memo.emplace(node, owner);
      if (inserted.second)
        compact[owner].push_back(std::move(node));
      else if (find(owner) != find(inserted.first->second)) {
        merge(find(owner), find(inserted.first->second));
        changed = true;
      }
    }
  }
  if (changed) return false;
  classes_.swap(compact);
  return true;
}

template <Language L, class A>
void EGraph<L, A>::rebuild_indexes() {
  stored_node_count_ = 0;
  for (Id id = 0; id < classes_.size(); ++id)
    if (find(id) == id) stored_node_count_ += classes_[id].size();
  memo_.clear();
  op_index_.clear();
  for (Id id = 0; id < classes_.size(); ++id) {
    if (find(id) != id) continue;
    for (const auto& node : classes_[id]) {
      memo_.emplace(node, id);
      auto& candidates = op_index_[node.discriminant()];
      if (candidates.empty() || candidates.back() != id)
        candidates.push_back(id);
    }
  }
}

template <Language L, class A>
void EGraph<L, A>::rebuild() {
  while (true) {
    close_congruence();
    if (propagate_analysis()) continue;
    if (compact_nodes()) break;
  }
  rebuild_indexes();
  clean_ = true;
}
template <Language L, class A>
const std::vector<L>& EGraph<L, A>::nodes(Id id) const {
  require_clean();
  return classes_.at(find(id));
}
template <Language L, class A>
const std::vector<Id>& EGraph<L, A>::classes_for_op(
    const typename L::Discriminant& op) const {
  require_clean();
  static const std::vector<Id> empty;
  const auto found = op_index_.find(op);
  return found == op_index_.end() ? empty : found->second;
}
template <Language L, class A>
std::vector<Id> EGraph<L, A>::classes() const {
  std::vector<Id> result;
  for (Id i = 0; i < parent_.size(); ++i)
    if (find(i) == i) result.push_back(i);
  return result;
}
template <Language L, class A>
std::size_t EGraph<L, A>::class_count() const {
  return classes().size();
}
template <Language L, class A>
std::size_t EGraph<L, A>::node_count() const {
  return stored_node_count_;
}

}  // namespace eggc
