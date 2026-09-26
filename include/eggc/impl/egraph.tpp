#pragma once
#include <algorithm>
#include <stdexcept>

namespace eggc {
template <Language L, class A>
void EGraph<L, A>::require_clean() const {
  if (!clean_) throw std::logic_error("query requires a rebuilt e-graph");
}
template <Language L, class A>
const typename A::Data& EGraph<L, A>::analysis_data(Id id) const {
  return analysis_data_.at(find(id)).value;
}
template <Language L, class A>
Id EGraph<L, A>::find(Id id) {
  return unions_.find(id);
}
template <Language L, class A>
Id EGraph<L, A>::find(Id id) const {
  return unions_.find(id);
}
template <Language L, class A>
void EGraph<L, A>::enqueue_repair(NodeId id) {
  if (arena_[id].active && !arena_[id].repair_queued) {
    arena_[id].repair_queued = true;
    repair_pending_.push_back(id);
  }
}
template <Language L, class A>
void EGraph<L, A>::enqueue_analysis(NodeId id) {
  if (arena_[id].active && !arena_[id].analysis_queued) {
    arena_[id].analysis_queued = true;
    analysis_pending_.push_back(id);
  }
}
template <Language L, class A>
void EGraph<L, A>::enqueue_modify(Id id) {
  if constexpr (requires(A& a, EGraph& g, Id i) { a.modify(g, i); }) {
    id = find(id);
    if (!modify_queued_[id]) {
      modify_queued_[id] = 1;
      modify_pending_.push_back(id);
    }
  }
}
template <Language L, class A>
Id EGraph<L, A>::add(L node) {
  for (Id& child : node.children_mut()) child = find(child);
  if (auto it = memo_.find(node); it != memo_.end())
    return find(arena_[it->second].owner);
  if (unions_.size() >= invalid_id)
    throw std::overflow_error("too many e-classes");
  Data data = analysis_.make(*this, node);
  const Id id = static_cast<Id>(unions_.size());
  const NodeId nid = arena_.size();
  if (explanations_) original_terms_.push_back(node);
  unions_.add();
  members_.push_back({nid});
  uses_.emplace_back();
  classes_.emplace_back();
  analysis_data_.push_back({std::move(data)});
  modify_queued_.push_back(0);
  arena_.push_back({std::move(node), id});
  memo_.emplace(arena_.back().node, nid);
  for (Id child : arena_.back().node.children()) uses_[child].insert(nid);
  dirty_classes_.insert(id);
  live_ids_.insert(id);
  ++live_classes_;
  ++live_nodes_;
  ++revision_;
  ++analysis_revision_;
  clean_ = false;
  enqueue_repair(nid);
  enqueue_analysis(nid);
  enqueue_modify(id);
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
  ids.reserve(expr.nodes.size());
  for (L node : expr.nodes) {
    for (Id& child : node.children_mut()) child = ids[child];
    ids.push_back(add(std::move(node)));
  }
  return ids.back();
}
template <Language L, class A>
bool EGraph<L, A>::merge(Id lhs, Id rhs, Justification justification) {
  Id a = find(lhs), b = find(rhs);
  if (a == b) return false;
  auto ordered = unions_.order(a, b);
  a = ordered.first;
  b = ordered.second;
  Data data = analysis_data_[a].value;
  auto result = analysis_.merge(data, analysis_data_[b].value);
  if (result == AnalysisMerge::Conflict)
    throw AnalysisConflict("e-class analysis conflict while merging classes");
  if (explanations_) {
    if (modifying_ && justification.kind == UnionKind::User) {
      justification.kind = UnionKind::Analysis;
      if (justification.name == "user") justification.name = "analysis";
    }
    proof_.add({lhs, rhs, std::move(justification)}, unions_.size());
  }
  analysis_data_[a].value = std::move(data);
  unions_.link(a, b);
  members_[a].insert(members_[a].end(), members_[b].begin(), members_[b].end());
  members_[b].clear();
  uses_[a].insert(uses_[b].begin(), uses_[b].end());
  uses_[b].clear();
  // Both sides' parents may need new facts, even if merge reports that the
  // winning class's data was unchanged.
  for (auto nid : uses_[a]) {
    enqueue_repair(nid);
    enqueue_analysis(nid);
  }
  dirty_classes_.insert(a);
  dirty_classes_.insert(b);
  live_ids_.erase(b);
  --live_classes_;
  ++revision_;
  if (result == AnalysisMerge::Changed) {
    ++analysis_revision_;
    ++revision_;
  }
  if (modifying_) ++rebuild_stats_.analysis_unions;
  clean_ = false;
  enqueue_modify(a);
  return true;
}
template <Language L, class A>
void EGraph<L, A>::repair(NodeId nid) {
  if (!arena_[nid].active) return;
  ++rebuild_stats_.repaired_nodes;
  L canonical = arena_[nid].node;
  for (Id& child : canonical.children_mut()) child = find(child);
  if (canonical != arena_[nid].node) {
    const auto old = memo_.find(arena_[nid].node);
    if (old != memo_.end() && old->second == nid) memo_.erase(old);
  }
  const auto existing = memo_.find(canonical);
  if (existing != memo_.end() && existing->second != nid) {
    const NodeId other = existing->second;
    Justification why{UnionKind::Congruence, "congruence", {}};
    if (explanations_) {
      for (std::size_t i = 0; i < canonical.children().size(); ++i)
        why.premises.emplace_back(arena_[nid].node.children()[i],
                                  arena_[other].node.children()[i]);
    }
    if (merge(arena_[nid].owner, arena_[other].owner, std::move(why)))
      ++rebuild_stats_.congruence_unions;
    for (Id child : arena_[nid].node.children()) uses_[find(child)].erase(nid);
    arena_[nid].active = false;
    --live_nodes_;
    dirty_classes_.insert(find(arena_[nid].owner));
  } else {
    arena_[nid].node = std::move(canonical);
    memo_.insert_or_assign(arena_[nid].node, nid);
    dirty_classes_.insert(find(arena_[nid].owner));
  }
}
template <Language L, class A>
void EGraph<L, A>::analyze(NodeId nid) {
  if (!arena_[nid].active) return;
  const Id owner = find(arena_[nid].owner);
  ++rebuild_stats_.analysis_evaluations;
  const Data inferred = analysis_.make(*this, arena_[nid].node);
  Data data = analysis_data_[owner].value;
  const auto result = analysis_.merge(data, inferred);
  if (result == AnalysisMerge::Conflict)
    throw AnalysisConflict("e-class analysis conflict while rebuilding");
  if (result == AnalysisMerge::Changed) {
    analysis_data_[owner].value = std::move(data);
    ++analysis_revision_;
    ++revision_;
    ++rebuild_stats_.analysis_changes;
    for (auto parent : uses_[owner]) enqueue_analysis(parent);
    enqueue_modify(owner);
  }
}
template <Language L, class A>
void EGraph<L, A>::refresh_views() {
  // Remove all old memberships before adding new ones, since a root can move.
  for (Id id : dirty_classes_)
    for (const auto& node : classes_[id]) {
      auto op = node.discriminant();
      op_classes_[op].erase(id);
      dirty_ops_.insert(std::move(op));
    }
  for (Id id : dirty_classes_) {
    ++rebuild_stats_.refreshed_classes;
    classes_[id].clear();
    if (find(id) != id) continue;
    auto& members = members_[id];
    std::erase_if(members, [&](NodeId nid) { return !arena_[nid].active; });
    std::sort(members.begin(), members.end());
    for (NodeId nid : members) {
      classes_[id].push_back(arena_[nid].node);
      auto op = arena_[nid].node.discriminant();
      op_classes_[op].insert(id);
      dirty_ops_.insert(std::move(op));
    }
  }
  dirty_classes_.clear();
}
template <Language L, class A>
void EGraph<L, A>::rebuild() {
  (void)rebuild({});
}
template <Language L, class A>
bool EGraph<L, A>::rebuild(const std::function<bool()>& should_stop) {
  if (rebuilding_)
    throw std::logic_error("analysis hook cannot recursively rebuild");
  rebuild_stats_ = {};
  if (clean_ && modify_pending_.empty()) return true;
  bool suspended = false;
  clean_ = false;
  rebuilding_ = true;
  rebuild_stats_.congruence_passes = 1;
  try {
    while (!repair_pending_.empty() || !analysis_pending_.empty() ||
           (!suspended && !modify_pending_.empty())) {
      if (should_stop && should_stop()) suspended = true;
      while (!repair_pending_.empty()) {
        const NodeId nid = repair_pending_.front();
        // Keep the item queued on exceptions; drop its flag during processing
        // so a new union can schedule a subsequent repair of this same node.
        arena_[nid].repair_queued = false;
        try {
          repair(nid);
        } catch (...) {
          arena_[nid].repair_queued = true;
          throw;
        }
        repair_pending_.pop_front();
      }
      if (!analysis_pending_.empty()) {
        const NodeId nid = analysis_pending_.front();
        arena_[nid].analysis_queued = false;
        try {
          analyze(nid);
        } catch (...) {
          arena_[nid].analysis_queued = true;
          throw;
        }
        analysis_pending_.pop_front();
        continue;
      }
      if constexpr (requires(A& a, EGraph& g, Id i) { a.modify(g, i); }) {
        if (!suspended && !modify_pending_.empty()) {
          const Id old = modify_pending_.front();
          modify_queued_[old] = 0;
          modifying_ = true;
          try {
            analysis_.modify(*this, find(old));
          } catch (...) {
            modifying_ = false;
            modify_queued_[old] = 1;
            throw;
          }
          modifying_ = false;
          ++rebuild_stats_.modifications;
          modify_pending_.pop_front();
        }
      }
    }
    refresh_views();
    clean_ = true;
    rebuilding_ = false;
    return !suspended;
  } catch (...) {
    rebuilding_ = false;
    throw;
  }
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
  if (dirty_ops_.contains(op)) {
    const auto& members = op_classes_.at(op);
    auto& cached = op_index_[op];
    cached.assign(members.begin(), members.end());
    dirty_ops_.erase(op);
    ++index_stats_.materializations;
    index_stats_.copied_candidate_ids += cached.size();
  }
  const auto it = op_index_.find(op);
  return it == op_index_.end() ? empty : it->second;
}
template <Language L, class A>
std::vector<Id> EGraph<L, A>::classes() const {
  return {live_ids_.begin(), live_ids_.end()};
}
template <Language L, class A>
std::vector<Id> EGraph<L, A>::parent_classes(Id id) const {
  require_clean();
  std::set<Id> result;
  for (auto nid : uses_.at(find(id)))
    if (arena_[nid].active) result.insert(find(arena_[nid].owner));
  return {result.begin(), result.end()};
}
template <Language L, class A>
std::optional<Id> EGraph<L, A>::lookup(L node) const {
  require_clean();
  for (Id& child : node.children_mut()) child = find(child);
  auto it = memo_.find(node);
  if (it == memo_.end()) return {};
  return find(arena_[it->second].owner);
}
template <Language L, class A>
std::optional<Id> EGraph<L, A>::lookup_expr(const RecExpr<L>& expr) const {
  require_clean();
  if (expr.nodes.empty())
    throw std::invalid_argument("cannot look up an empty expression");
  for (std::size_t i = 0; i < expr.nodes.size(); ++i)
    for (Id child : expr.nodes[i].children())
      if (child >= i)
        throw std::invalid_argument("expression children must precede parent");
  std::vector<Id> ids;
  for (auto node : expr.nodes) {
    for (Id& child : node.children_mut()) child = ids[child];
    auto found = lookup(std::move(node));
    if (!found) return {};
    ids.push_back(*found);
  }
  return ids.back();
}
template <Language L, class A>
void EGraph<L, A>::check_invariants() const {
  require_clean();
  std::size_t count = 0;
  std::unordered_set<L, NodeHash<L>> unique;
  for (Id id : live_ids_) {
    if (find(id) != id || classes_[id].size() != members_[id].size())
      throw std::logic_error("invalid class view");
    std::size_t view_index = 0;
    for (auto nid : members_[id]) {
      const auto& record = arena_[nid];
      if (classes_[id][view_index++] != record.node)
        throw std::logic_error("stale class view");
      if (!record.active || find(record.owner) != id ||
          !unique.insert(record.node).second)
        throw std::logic_error("invalid class membership");
      auto memo = memo_.find(record.node);
      if (memo == memo_.end() || memo->second != nid)
        throw std::logic_error("invalid memo");
      for (Id child : record.node.children())
        if (find(child) != child || !uses_[child].contains(nid))
          throw std::logic_error("invalid parent dependency");
      const auto& candidates = classes_for_op(record.node.discriminant());
      if (!std::binary_search(candidates.begin(), candidates.end(), id))
        throw std::logic_error("invalid operator index");
      ++count;
    }
  }
  if (count != live_nodes_ || live_ids_.size() != live_classes_ ||
      memo_.size() != count)
    throw std::logic_error("invalid graph counts");
  for (Id id : live_ids_)
    for (NodeId nid : uses_[id]) {
      if (!arena_[nid].active)
        throw std::logic_error("retired parent dependency");
      const auto children = arena_[nid].node.children();
      if (std::find(children.begin(), children.end(), id) == children.end())
        throw std::logic_error("spurious parent dependency");
    }
  for (const auto& [op, members] : op_classes_) {
    (void)members;
    classes_for_op(op);
  }
  for (const auto& [op, cached] : op_index_) {
    const auto found = op_classes_.find(op);
    if (found == op_classes_.end() ||
        std::vector<Id>(found->second.begin(), found->second.end()) != cached)
      throw std::logic_error("stale operator candidates");
    for (Id id : cached) {
      if (!live_ids_.contains(id) ||
          !std::any_of(
              classes_[id].begin(), classes_[id].end(),
              [&](const L& node) { return node.discriminant() == op; }))
        throw std::logic_error("spurious operator membership");
    }
  }
}
template <Language L, class A>
void EGraph<L, A>::enable_explanations() {
  if (!unions_.empty())
    throw std::logic_error("enable explanations before adding terms");
  explanations_ = true;
}
template <Language L, class A>
std::vector<ProofStep> EGraph<L, A>::explain_equivalence(Id lhs, Id rhs) const {
  require_clean();
  if (!explanations_) throw std::logic_error("explanations are disabled");
  if (find(lhs) != find(rhs))
    throw std::invalid_argument("terms are not equivalent");
  return proof_.path(lhs, rhs, unions_.size());
}
template <Language L, class A>
MemoryStats EGraph<L, A>::memory_stats() const {
  MemoryStats result{live_nodes_, arena_.size() - live_nodes_, arena_.size(),
                     unions_.size(), sizeof(*this)};
  auto& bytes = result.estimated_bytes;
  bytes += unions_.storage_bytes() + proof_.storage_bytes() +
           original_terms_.capacity() * sizeof(L);
  bytes += arena_.capacity() * sizeof(NodeRecord) +
           analysis_data_.capacity() * sizeof(AnalysisSlot);
  bytes += members_.capacity() * sizeof(std::vector<NodeId>) +
           classes_.capacity() * sizeof(std::vector<L>);
  bytes += uses_.capacity() * sizeof(std::unordered_set<NodeId>) +
           modify_queued_.capacity();
  for (const auto& members : members_)
    bytes += members.capacity() * sizeof(NodeId);
  for (const auto& nodes : classes_) bytes += nodes.capacity() * sizeof(L);
  for (const auto& uses : uses_)
    bytes += uses.bucket_count() * sizeof(void*) +
             uses.size() * (sizeof(NodeId) + 2 * sizeof(void*));
  bytes += memo_.bucket_count() * sizeof(void*) +
           memo_.size() * (sizeof(L) + sizeof(NodeId) + 2 * sizeof(void*));
  bytes += (live_ids_.size() + dirty_classes_.size()) *
           (sizeof(Id) + 3 * sizeof(void*));
  for (const auto& [op, ids] : op_classes_) {
    (void)op;
    bytes += sizeof(op) + ids.size() * (sizeof(Id) + 3 * sizeof(void*));
  }
  for (const auto& [op, ids] : op_index_) {
    (void)op;
    bytes += sizeof(op) + ids.capacity() * sizeof(Id);
  }
  bytes += (op_classes_.bucket_count() + op_index_.bucket_count() +
            dirty_ops_.bucket_count()) *
           sizeof(void*);
  bytes += dirty_ops_.size() *
           (sizeof(typename L::Discriminant) + 2 * sizeof(void*));
  bytes +=
      (repair_pending_.size() + analysis_pending_.size()) * sizeof(NodeId) +
      modify_pending_.size() * sizeof(Id);
  return result;
}
}  // namespace eggc
