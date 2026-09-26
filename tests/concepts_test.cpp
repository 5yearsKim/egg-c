#include <array>
#include <list>
#include <memory>
#include <span>

#include "eggc/all.hpp"

namespace {
struct Node {
  enum class Kind { Value };
  using Discriminant = Kind;
  int value = 0;
  std::vector<eggc::Id> operands;
  Kind discriminant() const { return Kind::Value; }
  const auto& children() const { return operands; }
  auto& children_mut() { return operands; }
  bool matches(const Node& other) const {
    return value == other.value && operands.size() == other.operands.size();
  }
  bool operator==(const Node&) const = default;
  std::size_t hash() const {
    std::size_t result = std::hash<int>{}(value);
    for (auto child : operands) eggc::hash_combine(result, child);
    return result;
  }
};
static_assert(eggc::Language<Node>);
static_assert(eggc::AnalysisFor<eggc::NoAnalysis<Node>, Node>);

// Borrowed views and fixed-size children remain supported. Nodes need not be
// default constructible; the graph stores complete values supplied by callers.
struct SpanNode : Node {
  std::span<const eggc::Id> children() const { return operands; }
  std::span<eggc::Id> children_mut() { return operands; }
};
struct ArrayNode : Node {
  std::array<eggc::Id, 0> children_storage;
  const auto& children() const { return children_storage; }
  auto& children_mut() { return children_storage; }
};
struct NonDefaultNode : Node {
  explicit NonDefaultNode(int v) { value = v; }
};
static_assert(eggc::Language<SpanNode>);
static_assert(eggc::Language<ArrayNode>);
static_assert(eggc::Language<NonDefaultNode>);
static_assert(!std::default_initializable<NonDefaultNode>);

struct MissingMethods {};
struct MissingDiscriminant : Node {
  void discriminant() const = delete;
};
struct MissingHash : Node {
  void hash() const = delete;
};
struct MissingMatches : Node {
  void matches(const MissingMatches&) const = delete;
};
struct MissingChildren : Node {
  void children() const = delete;
};
struct MissingMutableChildren : Node {
  void children_mut() = delete;
};
struct MissingEquality : Node {
  bool operator==(const MissingEquality&) const = delete;
};
struct WrongHash : Node {
  int hash() const { return 0; }
};
struct WrongMatches : Node {
  int matches(const WrongMatches&) const { return 1; }
};
struct WrongDiscriminant : Node {
  int discriminant() const { return 0; }
};
struct OwningChildrenTemporary : Node {
  auto children() const { return operands; }
};
struct ReadOnlyMutableChildren : Node {
  const auto& children_mut() { return operands; }
};
struct MutableConstChildren : Node {
  mutable std::vector<eggc::Id> mutable_operands;
  auto& children() const { return mutable_operands; }
};
struct WrongChildType : Node {
  std::vector<int> other_operands;
  const auto& children() const { return other_operands; }
  auto& children_mut() { return other_operands; }
};
struct NonIndexedChildren : Node {
  std::list<eggc::Id> other_operands;
  const auto& children() const { return other_operands; }
  auto& children_mut() { return other_operands; }
};
struct NonCopyableNode : Node {
  std::unique_ptr<int> data;
};
struct UnhashableKey {
  bool operator==(const UnhashableKey&) const = default;
};
struct UnhashableDiscriminant : Node {
  using Discriminant = UnhashableKey;
  Discriminant discriminant() const { return {}; }
};
static_assert(!eggc::Language<MissingMethods>);
static_assert(!eggc::Language<MissingDiscriminant>);
static_assert(!eggc::Language<MissingHash>);
static_assert(!eggc::Language<MissingMatches>);
static_assert(!eggc::Language<MissingChildren>);
static_assert(!eggc::Language<MissingMutableChildren>);
static_assert(!eggc::Language<MissingEquality>);
static_assert(!eggc::Language<WrongHash>);
static_assert(!eggc::Language<WrongMatches>);
static_assert(!eggc::Language<WrongDiscriminant>);
static_assert(!eggc::Language<OwningChildrenTemporary>);
static_assert(!eggc::Language<ReadOnlyMutableChildren>);
static_assert(!eggc::Language<MutableConstChildren>);
static_assert(!eggc::Language<WrongChildType>);
static_assert(!eggc::Language<NonIndexedChildren>);
static_assert(!eggc::Language<NonCopyableNode>);
static_assert(!eggc::Language<UnhashableDiscriminant>);

// Every public node container rejects incomplete nodes at its template
// boundary.
template <class T>
concept GraphAccepts = requires { typename eggc::EGraph<T>; };
template <class T>
concept PatternAccepts = requires { typename eggc::Pattern<T>; };
template <class T>
concept ExprAccepts = requires { typename eggc::RecExpr<T>; };
template <class T>
concept ExtractorAccepts = requires { typename eggc::Extractor<T>; };
template <class T>
concept RewriteAccepts = requires { typename eggc::Rewrite<T>; };
static_assert(GraphAccepts<Node> && !GraphAccepts<MissingHash>);
static_assert(PatternAccepts<Node> && !PatternAccepts<MissingMatches>);
static_assert(ExprAccepts<Node> && !ExprAccepts<WrongChildType>);
static_assert(ExtractorAccepts<Node> && !ExtractorAccepts<MissingChildren>);
static_assert(RewriteAccepts<Node> && !RewriteAccepts<MissingEquality>);

struct Analysis {
  using Data = std::optional<int>;
  template <class Graph>
  Data make(const Graph&, const Node& n) const {
    return n.value;
  }
  eggc::AnalysisMerge merge(Data& into, const Data& from) const {
    if (!from || into == from) return eggc::AnalysisMerge::Unchanged;
    if (into) return eggc::AnalysisMerge::Conflict;
    into = from;
    return eggc::AnalysisMerge::Changed;
  }
};
struct MissingData {};
struct MissingMake : Analysis {
  void make() const = delete;
};
struct MissingMerge : Analysis {
  void merge() const = delete;
};
struct WrongMake : Analysis {
  template <class Graph>
  int make(const Graph&, const Node&) const {
    return 0;
  }
};
struct WrongMerge : Analysis {
  bool merge(Data&, const Data&) const { return false; }
};
struct NonCopyableData : Analysis {
  using Data = std::unique_ptr<int>;
};
static_assert(eggc::AnalysisFor<Analysis, Node>);
static_assert(!eggc::AnalysisFor<MissingData, Node>);
static_assert(!eggc::AnalysisFor<MissingMake, Node>);
static_assert(!eggc::AnalysisFor<MissingMerge, Node>);
static_assert(!eggc::AnalysisFor<WrongMake, Node>);
static_assert(!eggc::AnalysisFor<WrongMerge, Node>);
static_assert(!eggc::AnalysisFor<NonCopyableData, Node>);

template <class A>
concept ExtractorAcceptsAnalysis =
    requires { typename eggc::Extractor<Node, A>; };
template <class A>
concept RewriteAcceptsAnalysis = requires { typename eggc::Rewrite<Node, A>; };
static_assert(ExtractorAcceptsAnalysis<Analysis> &&
              !ExtractorAcceptsAnalysis<WrongMake>);
static_assert(RewriteAcceptsAnalysis<Analysis> &&
              !RewriteAcceptsAnalysis<WrongMerge>);

// A can forward-declare its own graph alias before Data/methods are declared.
struct SelfAnalysis;
using SelfGraph = eggc::EGraph<Node, SelfAnalysis>;
struct SelfAnalysis : Analysis {
  Data make(const SelfGraph&, const Node& n) const { return n.value; }
};
static_assert(eggc::AnalysisFor<SelfAnalysis, Node>);
static_assert(sizeof(SelfGraph) > 0);
}  // namespace

int main() {
  // Instantiate actual algorithms for borrowed spans, arrays and nondefault
  // nodes, not just their contracts. No Graph-specific or TensorLang adapter is
  // needed.
  eggc::EGraph<SpanNode> spans;
  SpanNode leaf;
  auto child = spans.add(leaf);
  SpanNode parent;
  parent.operands = {child};
  auto root = spans.add(parent);
  spans.rebuild();
  auto [cost, expression] = eggc::Extractor<SpanNode>(spans).find_best(root);
  if (cost != 2 || expression.nodes.size() != 2) return 1;
  auto pattern = eggc::Pattern<SpanNode>::node(
      parent, {eggc::Pattern<SpanNode>::var("x")});
  if (eggc::match(spans, pattern, root).size() != 1) return 1;

  eggc::EGraph<ArrayNode> arrays;
  auto array_root = arrays.add(ArrayNode{});
  arrays.rebuild();
  if (eggc::Extractor<ArrayNode>(arrays).best_cost(array_root) != 1) return 1;

  eggc::EGraph<NonDefaultNode> nondefault;
  auto nondefault_root = nondefault.add(NonDefaultNode{7});
  nondefault.rebuild();
  if (eggc::Extractor<NonDefaultNode>(nondefault).best_cost(nondefault_root) !=
      1)
    return 1;

  SelfGraph analyzed;
  auto known = analyzed.add(Node{});
  return analyzed.analysis_data(known) == 0 ? 0 : 1;
}
