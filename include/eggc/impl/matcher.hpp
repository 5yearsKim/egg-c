#pragma once
#include <algorithm>
#include <numeric>
#include <set>
#include <unordered_set>

namespace eggc {
// Owns a validated snapshot. Numeric registers and an undo trail keep maps and
// recursive continuations out of the matching loop.
template <Language L> class CompiledPattern {
public:
  explicit CompiledPattern(Pattern<L> pattern,
                           std::vector<std::string> variables = {})
      : pattern_(std::move(pattern)), variables_(std::move(variables)) {
    pattern_.validate();
    if (variables_.empty())
      variables_ = pattern_.variables();
    std::set<std::string> unique;
    for (const auto &name : variables_)
      if (name.size() < 2 || name.front() != '?' || !unique.insert(name).second)
        throw std::invalid_argument("invalid compiled variable list");
    slots_.resize(pattern_.nodes.size(), invalid_id);
    free_.resize(pattern_.nodes.size());
    std::vector<std::size_t> weights(pattern_.nodes.size(), 1);
    for (std::size_t i = 0; i < pattern_.nodes.size(); ++i) {
      if (auto var = std::get_if<Var>(&pattern_.nodes[i])) {
        const auto found =
            std::find(variables_.begin(), variables_.end(), var->name);
        if (found == variables_.end())
          throw std::invalid_argument("unbound compiled variable");
        slots_[i] = static_cast<Id>(found - variables_.begin());
        free_[i].push_back(slots_[i]);
      } else {
        for (Id child : std::get<L>(pattern_.nodes[i]).children()) {
          free_[i].insert(free_[i].end(), free_[child].begin(),
                          free_[child].end());
          weights[i] =
              std::min<std::size_t>(100000, weights[i] + weights[child]);
        }
        std::sort(free_[i].begin(), free_[i].end());
        free_[i].erase(std::unique(free_[i].begin(), free_[i].end()),
                       free_[i].end());
      }
    }
    struct Task {
      Id entry;
      std::size_t reg;
      bool exit;
      std::size_t instruction;
    };
    std::vector<Task> tasks{
        {static_cast<Id>(pattern_.nodes.size() - 1), 0, false, 0}};
    registers_ = 1;
    while (!tasks.empty()) {
      const auto task = tasks.back();
      tasks.pop_back();
      if (task.exit) {
        code_[task.instruction].end = code_.size();
        continue;
      }
      if (code_.size() >= 100000)
        throw std::length_error("compiled pattern exceeds instruction budget");
      const auto pc = code_.size();
      code_.push_back({task.entry, task.reg, registers_, pc + 1});
      if (const auto node = std::get_if<L>(&pattern_.nodes[task.entry])) {
        const auto base = registers_;
        registers_ += node->children().size();
        tasks.push_back({0, 0, true, pc});
        std::vector<std::size_t> order(node->children().size());
        std::iota(order.begin(), order.end(), 0);
        std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) {
          return weights[node->children()[a]] > weights[node->children()[b]];
        });
        for (auto it = order.rbegin(); it != order.rend(); ++it)
          tasks.push_back({node->children()[*it], base + *it, false, 0});
      }
    }
  }
  const std::vector<std::string> &variables() const noexcept {
    return variables_;
  }
  const Pattern<L> &pattern() const noexcept { return pattern_; }
  Substitution substitution(const std::vector<Id> &bindings) const {
    if (bindings.size() != variables_.size())
      throw std::invalid_argument("invalid binding size");
    Substitution result;
    for (std::size_t i = 0; i < variables_.size(); ++i)
      if (bindings[i] != invalid_id)
        result.emplace(variables_[i], bindings[i]);
    return result;
  }
  template <class A>
  Id instantiate(EGraph<L, A> &graph, const std::vector<Id> &bindings) const {
    if (bindings.size() != variables_.size())
      throw std::invalid_argument("invalid binding size");
    for (auto slot : slots_)
      if (slot != invalid_id)
        graph.find(bindings[slot]);
    std::vector<Id> ids;
    ids.reserve(pattern_.nodes.size());
    for (std::size_t i = 0; i < pattern_.nodes.size(); ++i) {
      if (slots_[i] != invalid_id)
        ids.push_back(graph.find(bindings[slots_[i]]));
      else {
        L node = std::get<L>(pattern_.nodes[i]);
        for (Id &child : node.children_mut())
          child = ids[child];
        ids.push_back(graph.add(std::move(node)));
      }
    }
    return ids.back();
  }
  template <class A, class Callback>
  bool search(const EGraph<L, A> &graph, Id root, Callback &&emit,
              const StopCheck &stop = {}, std::vector<Id> bindings = {}) const {
    graph.require_clean();
    if (bindings.empty())
      bindings.resize(variables_.size(), invalid_id);
    if (bindings.size() != variables_.size())
      throw std::invalid_argument("invalid binding size");
    for (Id &id : bindings)
      if (id != invalid_id)
        id = graph.find(id);
    std::vector<Id> regs(registers_, invalid_id);
    regs[0] = graph.find(root);
    std::vector<Id> trail;
    struct Frame {
      std::size_t pc, next, checkpoint;
      Id eclass;
    };
    std::vector<Frame> stack;
    std::unordered_set<std::vector<Id>, BindingHash> seen;
    const auto rollback = [&](std::size_t checkpoint) {
      while (trail.size() > checkpoint) {
        bindings[trail.back()] = invalid_id;
        trail.pop_back();
      }
    };
    const auto next_choice = [&]() {
      while (!stack.empty()) {
        auto &frame = stack.back();
        rollback(frame.checkpoint);
        const auto &instruction = code_[frame.pc];
        const auto &prototype = std::get<L>(pattern_.nodes[instruction.entry]);
        const auto &nodes = graph.nodes(frame.eclass);
        while (frame.next < nodes.size()) {
          if (stop && stop())
            return false;
          const auto &node = nodes[frame.next++];
          if (!prototype.matches(node) ||
              prototype.children().size() != node.children().size())
            continue;
          for (std::size_t i = 0; i < node.children().size(); ++i)
            regs[instruction.out + i] = graph.find(node.children()[i]);
          return true;
        }
        stack.pop_back();
      }
      return false;
    };
    std::size_t pc = 0;
    for (;;) {
      if (stop && stop())
        return false;
      bool failed = false;
      if (pc == code_.size()) {
        if (seen.insert(bindings).second && !emit(bindings))
          return false;
        failed = true;
      } else {
        const auto &instruction = code_[pc];
        const Id eclass = graph.find(regs[instruction.reg]);
        const Id slot = slots_[instruction.entry];
        if (slot != invalid_id) {
          if (bindings[slot] == invalid_id) {
            bindings[slot] = eclass;
            trail.push_back(slot);
          } else if (bindings[slot] != eclass)
            failed = true;
          if (!failed) {
            ++pc;
            continue;
          }
        } else {
          // Exact matching languages can look up ground/bound subexpressions.
          // General matches() predicates continue to enumerate candidates.
          bool ground = false;
          if constexpr (requires { L::exact_matches; }) {
            if constexpr (L::exact_matches) {
              ground = std::all_of(free_[instruction.entry].begin(),
                                   free_[instruction.entry].end(), [&](Id v) {
                                     return bindings[v] != invalid_id;
                                   });
            }
          }
          if (ground) {
            auto found = lookup_bound(graph, instruction.entry, bindings, stop);
            if (stop && stop())
              return false;
            if (found && *found == eclass) {
              pc = instruction.end;
              continue;
            }
            failed = true;
          } else {
            stack.push_back({pc, 0, trail.size(), eclass});
            if (next_choice()) {
              pc = stack.back().pc + 1;
              continue;
            }
            if (stop && stop())
              return false;
            return true;
          }
        }
      }
      if (failed) {
        if (!next_choice())
          return !(stop && stop());
        pc = stack.back().pc + 1;
      }
    }
  }

private:
  struct Instruction {
    Id entry;
    std::size_t reg, out, end;
  };
  struct BindingHash {
    std::size_t operator()(const std::vector<Id> &values) const {
      std::size_t hash = 0;
      for (auto value : values)
        hash_combine(hash, value);
      return hash;
    }
  };
  template <class A>
  std::optional<Id> lookup_bound(const EGraph<L, A> &graph, Id root,
                                 const std::vector<Id> &bindings,
                                 const StopCheck &stop) const {
    std::vector<std::optional<Id>> ids(pattern_.nodes.size());
    std::vector<std::pair<Id, bool>> stack{{root, false}};
    while (!stack.empty()) {
      if (stop && stop())
        return {};
      auto [id, expanded] = stack.back();
      stack.pop_back();
      if (ids[id])
        continue;
      if (slots_[id] != invalid_id) {
        ids[id] = bindings[slots_[id]];
        continue;
      }
      auto node = std::get<L>(pattern_.nodes[id]);
      if (!expanded) {
        stack.push_back({id, true});
        for (Id child : node.children())
          if (!ids[child])
            stack.push_back({child, false});
      } else {
        for (Id &child : node.children_mut()) {
          if (!ids[child])
            return {};
          child = *ids[child];
        }
        ids[id] = graph.lookup(std::move(node));
        if (!ids[id])
          return {};
      }
    }
    return ids[root];
  }
  Pattern<L> pattern_;
  std::vector<std::string> variables_;
  std::vector<Id> slots_;
  std::vector<std::vector<Id>> free_;
  std::vector<Instruction> code_;
  std::size_t registers_ = 0;
};
} // namespace eggc
