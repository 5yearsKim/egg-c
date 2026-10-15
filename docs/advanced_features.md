# Advanced features

After the tutorials, use these APIs to inspect equality, choose shared
expressions, and join patterns. The
[advanced example](https://github.com/5yearsKim/c-egg/blob/main/examples/advanced_features.cpp)
demonstrates them together.

## Record explanations

Call `graph.enable_explanations()` before adding expressions or applying
rewrites. After establishing an equality, call
`graph.explain_equivalence(lhs, rhs)` to inspect the recorded steps and their
justifications.

`eggc::verify_rewrites(graph, rules)` replays recorded equations against the
supplied rewrite rules. It is a separate check from inspecting the stored
explanation. The
[reusable runner example](https://github.com/5yearsKim/c-egg/blob/main/examples/reusable_runner.cpp)
enables explanations before running and checks the recorded rewrites.

## Extract a shared expression

`Extractor` uses tree costs, so a repeated child contributes its cost each
time. `DagExtractor` can account for sharing by charging once per selected
e-class.

```cpp
auto result = eggc::DagExtractor<eggc::SymbolLang>(graph).solve(root);
```

Check `result.cost` before using the result and `result.optimal` before
claiming an optimum. DAG extraction has exponential worst-case complexity.
`DagOptions` provides state, frontier, and time limits; a limited search may
return an incumbent without proving optimality.

## Join pattern clauses

`MultiPattern` joins clauses through shared variables. For example, the
advanced example searches for `(left ?x ?y)` and `(right ?x ?y)` together:
both clauses must agree on the e-classes bound to `?x` and `?y`.

Use this when one rewrite needs evidence from several patterns rather than
a single expression tree. Rebuild the graph before searching it manually.

## Inspect the graph

`eggc::to_dot(graph)` produces Graphviz DOT text for visualization.
`graph.check_invariants()` checks internal graph consistency.

The graph retains a persistent node arena and tracks parent uses and pending
work. See [Code structure](architecture.md) for the implementation map and
[Performance and diagnostics](performance.md) for rebuild and allocation
counters.

## Run the example

From the repository root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target advanced_features
./build/examples/advanced_features
```

Or use Bazel:

```sh
bazel run //examples:advanced_features
```

The example verifies an explanation, compares tree and DAG extraction,
checks a joined match, and exports the graph.
