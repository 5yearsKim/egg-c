#pragma once
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "analysis.hpp"
#include "expr.hpp"
#include "language.hpp"

namespace eggc {
template <Language L, class A = NoAnalysis<L>>
class EGraph {
  // Do not constrain the forward declaration on AnalysisFor: A may be
  // incomplete when declaring its make(const EGraph<L, A>&, ...) method.
  static_assert(
      AnalysisFor<A, L>,
      "EGraph analysis must provide copyable Data, make(graph, node) "
      "returning Data, and merge(Data&, const Data&) returning AnalysisMerge");

 public:
  using Node = L;
  using Analysis = A;
  using Data = typename A::Data;
  explicit EGraph(A analysis = {}) : analysis_(std::move(analysis)) {}
  Id add(L node);
  Id add_expr(const RecExpr<L>& expr);
  Id find(Id id);
  Id find(Id id) const;
  bool merge(Id a, Id b);
  void rebuild();
  const std::vector<L>& nodes(Id id) const;
  const std::vector<Id>& classes_for_op(
      const typename L::Discriminant& op) const;
  std::vector<Id> classes() const;
  std::size_t class_count() const;
  std::size_t node_count() const;
  std::uint64_t revision() const noexcept { return revision_; }
  std::uint64_t analysis_revision() const noexcept {
    return analysis_revision_;
  }
  bool is_clean() const noexcept { return clean_; }
  void require_clean() const;
  // Available during make()/rebuild(); use only the facts of operand classes.
  const Data& analysis_data(Id id) const;

 private:
  void close_congruence();
  bool propagate_analysis();
  bool compact_nodes();
  void rebuild_indexes();
  std::vector<Id> parent_;
  std::vector<unsigned> rank_;
  std::vector<std::vector<L>> classes_;
  std::unordered_map<L, Id, NodeHash<L>> memo_;
  A analysis_;
  std::vector<Data> analysis_data_;
  std::unordered_map<typename L::Discriminant, std::vector<Id>> op_index_;
  std::size_t stored_node_count_ = 0;
  std::uint64_t revision_ = 0;
  std::uint64_t analysis_revision_ = 0;
  bool clean_ = true;
};
}  // namespace eggc
#include "egraph.tpp"
