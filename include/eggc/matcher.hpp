#pragma once
#include "detail/binding_set.hpp"
#include "replacement.hpp"
namespace eggc {
// One workspace per active search; do not mutate or share it with nested
// searches.
struct MatcherWorkspace {
  struct Frame {
    std::size_t pc, next, checkpoint;
    Id eclass;
  };
  std::vector<Id> bindings, registers, trail, lookup_ids;
  std::vector<Frame> frames;
  detail::BindingSet seen;
  std::vector<std::size_t> lookup_stamps;
  std::size_t lookup_generation = 0;
  std::vector<std::pair<Id, bool>> lookup_stack;
  std::size_t pc = 0;
  bool backtrack = false, finished = false;
  const void* graph_identity = nullptr;
  const void* program_identity = nullptr;
  std::uint64_t revision = 0;
};

enum class MatchStatus { Match, Done, Cancelled };

// Validated immutable matcher program with numeric variable slots.
template <Language L>
class CompiledPattern : public CompiledReplacement<L> {
 public:
  explicit CompiledPattern(Pattern<L> pattern,
                           std::vector<std::string> variables = {});
  template <class A, class Callback>
  bool search(const EGraph<L, A>& graph, Id root, Callback&& emit,
              const StopCheck& stop = {}, std::vector<Id> seed = {}) const;
  template <class A, class Callback>
  bool search(const EGraph<L, A>& graph, Id root, MatcherWorkspace& workspace,
              Callback&& emit, const StopCheck& stop = {},
              std::vector<Id> seed = {}) const;
  template <class A>
  void start(const EGraph<L, A>& graph, Id root, MatcherWorkspace& workspace,
             std::vector<Id> seed = {}) const;
  template <class A>
  MatchStatus next(const EGraph<L, A>& graph, MatcherWorkspace& workspace,
                   const StopCheck& stop = {}) const;

 private:
  struct Instruction {
    Id entry;
    std::size_t reg, out, end;
  };
  template <class A>
  std::optional<Id> lookup_bound(const EGraph<L, A>& graph, Id root,
                                 const std::vector<Id>& bindings,
                                 const StopCheck& stop,
                                 MatcherWorkspace& workspace) const;
  using CompiledReplacement<L>::pattern_;
  using CompiledReplacement<L>::variables_;
  using CompiledReplacement<L>::slots_;
  std::vector<std::vector<Id>> free_;
  std::vector<Instruction> code_;
  std::size_t registers_ = 0;
};
template <Language L, class A, class Callback>
  requires AnalysisFor<A, L>
bool search_matches(const EGraph<L, A>& graph, const Pattern<L>& pattern,
                    Id eclass, Callback&& on_match,
                    const StopCheck& should_stop = {});
template <Language L, class A>
  requires AnalysisFor<A, L>
std::vector<Substitution> match(const EGraph<L, A>& graph,
                                const Pattern<L>& pattern, Id eclass);
}  // namespace eggc
#include "impl/matcher.tpp"
#include "impl/pattern.tpp"
