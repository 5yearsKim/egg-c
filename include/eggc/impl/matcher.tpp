#pragma once
#include <algorithm>
#include <numeric>
namespace eggc {
template <Language L>
CompiledPattern<L>::CompiledPattern(Pattern<L> pattern,
                                    std::vector<std::string> variables)
    : CompiledReplacement<L>(std::move(pattern), std::move(variables)) {
  free_.resize(pattern_.nodes.size());
  std::vector<std::size_t> weights(pattern_.nodes.size(), 1);
  for (std::size_t i = 0; i < pattern_.nodes.size(); ++i) {
    if (std::holds_alternative<Var>(pattern_.nodes[i])) {
      free_[i].push_back(slots_[i]);
    } else {
      for (Id child : std::get<L>(pattern_.nodes[i]).children()) {
        free_[i].insert(free_[i].end(), free_[child].begin(),
                        free_[child].end());
        weights[i] = std::min<std::size_t>(100000, weights[i] + weights[child]);
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
template <Language L>
template <class A, class Callback>
bool CompiledPattern<L>::search(const EGraph<L, A>& graph, Id root,
                                Callback&& emit, const StopCheck& stop,
                                std::vector<Id> bindings) const {
  MatcherWorkspace workspace;
  return search(graph, root, workspace, std::forward<Callback>(emit), stop,
                std::move(bindings));
}
template <Language L>
template <class A, class Callback>
bool CompiledPattern<L>::search(const EGraph<L, A>& graph, Id root,
                                MatcherWorkspace& workspace, Callback&& emit,
                                const StopCheck& stop,
                                std::vector<Id> seed) const {
  start(graph, root, workspace, std::move(seed));
  for (;;) {
    const auto status = next(graph, workspace, stop);
    if (status != MatchStatus::Match) return status == MatchStatus::Done;
    if (!emit(workspace.bindings)) return false;
  }
}
template <Language L>
template <class A>
void CompiledPattern<L>::start(const EGraph<L, A>& graph, Id root,
                               MatcherWorkspace& workspace,
                               std::vector<Id> seed) const {
  graph.require_clean();
  auto& bindings = workspace.bindings;
  bindings.assign(variables_.size(), invalid_id);
  if (!seed.empty()) bindings.assign(seed.begin(), seed.end());
  if (bindings.size() != variables_.size())
    throw std::invalid_argument("invalid binding size");
  for (Id& id : bindings)
    if (id != invalid_id) id = graph.find(id);
  auto& regs = workspace.registers;
  regs.assign(registers_, invalid_id);
  regs[0] = graph.find(root);
  auto& trail = workspace.trail;
  trail.clear();
  auto& stack = workspace.frames;
  stack.clear();
  auto& seen = workspace.seen;
  seen.reset(variables_.size());
  workspace.graph_identity = &graph;
  workspace.program_identity = this;
  workspace.revision = graph.revision();
  workspace.pc = 0;
  workspace.backtrack = false;
  workspace.finished = false;
}
template <Language L>
template <class A>
MatchStatus CompiledPattern<L>::next(const EGraph<L, A>& graph,
                                     MatcherWorkspace& workspace,
                                     const StopCheck& stop) const {
  graph.require_clean();
  if (workspace.program_identity != this ||
      workspace.graph_identity != &graph ||
      workspace.revision != graph.revision())
    throw std::logic_error("matcher cursor graph or program changed");
  if (workspace.finished) return MatchStatus::Done;
  auto& bindings = workspace.bindings;
  auto& regs = workspace.registers;
  auto& trail = workspace.trail;
  auto& stack = workspace.frames;
  auto& seen = workspace.seen;
  const auto cancelled = [&] {
    workspace.finished = true;
    return MatchStatus::Cancelled;
  };
  const auto rollback = [&](std::size_t checkpoint) {
    while (trail.size() > checkpoint) {
      bindings[trail.back()] = invalid_id;
      trail.pop_back();
    }
  };
  const auto next_choice = [&]() {
    while (!stack.empty()) {
      auto& frame = stack.back();
      rollback(frame.checkpoint);
      const auto& instruction = code_[frame.pc];
      const auto& prototype = std::get<L>(pattern_.nodes[instruction.entry]);
      const auto& nodes = graph.nodes(frame.eclass);
      while (frame.next < nodes.size()) {
        if (stop && stop()) return false;
        const auto& node = nodes[frame.next++];
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
  auto& pc = workspace.pc;
  for (;;) {
    if (stop && stop()) return cancelled();
    bool failed = workspace.backtrack;
    workspace.backtrack = false;
    if (!failed && pc == code_.size()) {
      if (seen.insert(bindings)) {
        workspace.backtrack = true;
        return MatchStatus::Match;
      }
      failed = true;
    } else if (!failed) {
      const auto& instruction = code_[pc];
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
          auto found =
              lookup_bound(graph, instruction.entry, bindings, stop, workspace);
          if (stop && stop()) return cancelled();
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
          if (stop && stop()) return cancelled();
          workspace.finished = true;
          return MatchStatus::Done;
        }
      }
    }
    if (failed) {
      if (!next_choice()) {
        if (stop && stop()) return cancelled();
        workspace.finished = true;
        return MatchStatus::Done;
      }
      pc = stack.back().pc + 1;
    }
  }
}
template <Language L>
template <class A>
std::optional<Id> CompiledPattern<L>::lookup_bound(
    const EGraph<L, A>& graph, Id root, const std::vector<Id>& bindings,
    const StopCheck& stop, MatcherWorkspace& workspace) const {
  auto& ids = workspace.lookup_ids;
  ids.resize(pattern_.nodes.size());
  auto& stamps = workspace.lookup_stamps;
  stamps.resize(pattern_.nodes.size(), 0);
  if (++workspace.lookup_generation == 0) {
    std::fill(stamps.begin(), stamps.end(), 0);
    ++workspace.lookup_generation;
  }
  const auto generation = workspace.lookup_generation;
  auto& stack = workspace.lookup_stack;
  stack.clear();
  stack.push_back({root, false});
  while (!stack.empty()) {
    if (stop && stop()) return {};
    auto [id, expanded] = stack.back();
    stack.pop_back();
    if (stamps[id] == generation) continue;
    if (slots_[id] != invalid_id) {
      ids[id] = bindings[slots_[id]];
      stamps[id] = generation;
      continue;
    }
    auto node = std::get<L>(pattern_.nodes[id]);
    if (!expanded) {
      stack.push_back({id, true});
      for (Id child : node.children())
        if (stamps[child] != generation) stack.push_back({child, false});
    } else {
      for (Id& child : node.children_mut()) {
        if (stamps[child] != generation) return {};
        child = ids[child];
      }
      auto found = graph.lookup(std::move(node));
      if (!found) return {};
      ids[id] = *found;
      stamps[id] = generation;
    }
  }
  return ids[root];
}
}  // namespace eggc
