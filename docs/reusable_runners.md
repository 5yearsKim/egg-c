# Reusable runners

Use `run(graph, rules)` for a single optimization. Use `CompiledRules` when
several graphs share the same rules, and `Runner` when execution needs to
continue across several calls.

The [complete example](https://github.com/5yearsKim/c-egg/blob/main/examples/reusable_runner.cpp)
combines rule reuse, execution slices, extraction, and rewrite verification.

## Compile rules once

```cpp
using Node = eggc::SymbolLang;
std::vector<eggc::Rewrite<Node>> rules{
    eggc::rewrite("add-zero", "(+ ?x 0)", "?x")};
eggc::CompiledRules<Node> compiled_rules(rules);
```

`CompiledRules` shares an immutable program. Reuse it with separate graphs;
each runner owns its execution buffers and scheduling state.

## Run in slices

```cpp
eggc::EGraph<Node> graph;
auto root = graph.add_expr(eggc::parse_expr("(+ (+ a 0) 0)"));
eggc::Runner<Node> runner(graph, compiled_rules);

eggc::RunOptions slice;
slice.iteration_limit = 1;
runner.resume(slice);
runner.resume(slice);

auto report = runner.report();
```

Each `resume()` allows the specified number of additional iterations and a
fresh time budget. A later call searches a partially completed iteration
again after rebuilding. Inspect `report.reason` to distinguish saturation
from a limit. Limits on time and graph-accounted memory are cooperative.

## Extract from selected roots

```cpp
eggc::Extractor<Node> selected(graph, std::vector{root});
auto [cost, best] = selected.find_best(root);
```

Supplying roots restricts extraction to classes reachable from them. The
default policy counts AST nodes. For this input, the best result is `a`,
with cost `1`. Construct a new extractor after changing the graph.

## Run the complete example

From the repository root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target reusable_runner
./build/examples/reusable_runner
```

Or use Bazel:

```sh
bazel run //examples:reusable_runner
```

The example checks that the first graph saturates across two resume calls,
then reuses the compiled rules to simplify `(+ b 0)` in a second graph.
