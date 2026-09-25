#pragma once
#include "analysis.hpp"
#include "enode.hpp"
#include <cstdint>
#include "expr.hpp"
#include <memory>
#include <map>
#include <unordered_map>
#include <vector>

namespace eggc {
class EGraph;
namespace testing { void rebuild_full_scan(EGraph& graph); }
class EGraph {
public:
    explicit EGraph(std::shared_ptr<EClassAnalysis> analysis = {});
    Id add(ENode node);
    Id add(const std::string& op, std::vector<Id> children = {});
    Id add_expr(const RecExpr& expr);
    Id find(Id id);
    Id find(Id id) const;
    bool merge(Id a, Id b);
    void rebuild();
    const std::vector<ENode>& nodes(Id id) const;
    const std::vector<Id>& classes_for_op(const std::string& op, std::size_t arity) const;
    std::vector<Id> classes() const;
    std::size_t class_count() const;
    std::size_t node_count() const;
    std::uint64_t revision() const noexcept { return revision_; }
    std::uint64_t analysis_revision() const noexcept { return analysis_revision_; }
    bool is_clean() const noexcept { return clean_; }
    bool has_analysis() const noexcept { return static_cast<bool>(analysis_); }
    const std::any& analysis_data(Id id) const;

private:
    friend void testing::rebuild_full_scan(EGraph& graph);
    std::vector<Id> parent_;
    std::vector<unsigned> rank_;
    std::vector<std::vector<ENode>> classes_;
    std::unordered_map<ENode, Id, ENodeHash> memo_;
    std::shared_ptr<EClassAnalysis> analysis_;
    std::vector<std::any> analysis_data_;
    std::map<std::pair<std::string, std::size_t>, std::vector<Id>> op_index_;
    std::size_t stored_node_count_ = 0;
    std::uint64_t revision_ = 0;
    std::uint64_t analysis_revision_ = 0;
    bool clean_ = true;
};
}
