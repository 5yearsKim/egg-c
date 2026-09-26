#pragma once
#include <memory>

#include "rewrite.hpp"
namespace eggc {
// A conjunction of clauses sharing numeric variable bindings.
template <Language L>
class MultiPattern {
 public:
  using Clause = std::pair<std::string, Pattern<L>>;
  explicit MultiPattern(std::vector<Clause> clauses);
  const std::vector<std::string>& variables() const noexcept {
    return variables_;
  }
  template <class A, class Callback>
  bool search(const EGraph<L, A>& graph, Callback&& emit,
              const StopCheck& stop = {}) const;
  template <class A>
  std::vector<Substitution> match(const EGraph<L, A>& graph) const;

 private:
  std::vector<Clause> clauses_;
  std::vector<std::string> variables_;
  std::vector<CompiledPattern<L>> compiled_;
  std::vector<std::size_t> root_slots_;
};
template <Language L, class A = NoAnalysis<L>>
Rewrite<L, A> multi_rewrite(std::string name, MultiPattern<L> lhs,
                            std::string target, Pattern<L> rhs);
}  // namespace eggc
#include "impl/multipattern.tpp"
