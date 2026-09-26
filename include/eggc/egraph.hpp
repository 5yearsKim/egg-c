#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "analysis.hpp"
#include "expr.hpp"
#include "language.hpp"

namespace eggc {
struct RebuildStats {
  std::size_t congruence_passes = 0;
  std::size_t congruence_unions = 0;
  std::size_t analysis_evaluations = 0;
  std::size_t analysis_changes = 0;
  std::size_t analysis_unions = 0;
  std::size_t repaired_nodes = 0;
  std::size_t refreshed_classes = 0;
  std::size_t modifications = 0;
};
enum class UnionKind { User, Rewrite, Congruence, Analysis };
struct Justification {
  UnionKind kind = UnionKind::User;
  std::string name = "user";
  std::vector<std::pair<Id, Id>> premises;
};
struct ProofStep {
  Id lhs;
  Id rhs;
  Justification justification;
};

template <Language L, class A = NoAnalysis<L>> class EGraph {
  static_assert(AnalysisFor<A, L>, "invalid e-class analysis contract");

public:
  using Node = L;
  using Analysis = A;
  using Data = typename A::Data;
  explicit EGraph(A analysis = {}) : analysis_(std::move(analysis)) {}
  Id add(L node);
  Id add_expr(const RecExpr<L> &expr);
  Id find(Id id);
  Id find(Id id) const;
  bool merge(Id a, Id b, Justification justification = {});
  void rebuild();
  // Cooperative cancellation suspends modification hooks, then finishes
  // structural/analysis repair so queries remain valid. A later rebuild
  // resumes.
  bool rebuild(const std::function<bool()> &should_stop);
  const std::vector<L> &nodes(Id id) const;
  const std::vector<Id> &
  classes_for_op(const typename L::Discriminant &op) const;
  std::vector<Id> classes() const;
  std::vector<Id> parent_classes(Id id) const;
  std::size_t class_count() const noexcept { return live_classes_; }
  std::size_t node_count() const noexcept { return live_nodes_; }
  std::uint64_t revision() const noexcept { return revision_; }
  std::uint64_t analysis_revision() const noexcept {
    return analysis_revision_;
  }
  bool is_clean() const noexcept { return clean_; }
  const RebuildStats &last_rebuild_stats() const noexcept {
    return rebuild_stats_;
  }
  void require_clean() const;
  const Data &analysis_data(Id id) const;
  std::optional<Id> lookup(L node) const;
  std::optional<Id> lookup_expr(const RecExpr<L> &expr) const;
  // References returned by queries may be invalidated by any mutation.
  void check_invariants() const;
  // Enable before inserting terms so every equality has recorded provenance.
  void enable_explanations();
  bool explanations_enabled() const noexcept { return explanations_; }
  std::vector<ProofStep> explain_equivalence(Id lhs, Id rhs) const;

private:
  using NodeId = std::size_t;
  struct NodeRecord {
    L node;
    Id owner;
    bool active = true;
    bool repair_queued = false;
    bool analysis_queued = false;
  };
  struct AnalysisSlot {
    Data value;
  };
  void enqueue_repair(NodeId id);
  void enqueue_analysis(NodeId id);
  void enqueue_modify(Id id);
  void repair(NodeId id);
  void analyze(NodeId id);
  void refresh_views();
  std::vector<Id> parent_;
  std::vector<unsigned> rank_;
  std::vector<NodeRecord> arena_;
  std::vector<std::vector<NodeId>> members_;
  std::vector<std::unordered_set<NodeId>> uses_;
  std::vector<std::vector<L>> classes_;
  std::unordered_map<L, NodeId, NodeHash<L>> memo_;
  A analysis_;
  std::vector<AnalysisSlot> analysis_data_;
  std::deque<NodeId> repair_pending_, analysis_pending_;
  std::deque<Id> modify_pending_;
  std::vector<unsigned char> modify_queued_;
  std::set<Id> dirty_classes_;
  std::unordered_map<typename L::Discriminant, std::set<Id>> op_classes_;
  std::unordered_map<typename L::Discriminant, std::vector<Id>> op_index_;
  std::set<Id> live_ids_;
  std::size_t live_nodes_ = 0, live_classes_ = 0;
  std::uint64_t revision_ = 0, analysis_revision_ = 0;
  bool clean_ = true, rebuilding_ = false, modifying_ = false;
  bool explanations_ = false;
  std::vector<ProofStep> proof_;
  RebuildStats rebuild_stats_;
};
} // namespace eggc
#include "impl/egraph.tpp"
