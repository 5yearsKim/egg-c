# Advanced engine features

The [advanced example](../examples/advanced_features.cpp) demonstrates DAG
extraction, joined patterns, lookup, and equality provenance:

```sh
bazel run //examples:advanced_features
```

## Persistent rebuilding

The graph stores nodes in an arena with stable internal handles. E-class IDs
remain valid after unions; use `find()` to canonicalize them. Parent-use lists
and separate congruence, analysis, and modification queues persist across
rebuilds. A union schedules parents from both classes. Duplicate canonical
nodes are retired, and only affected class views and operator indexes are
refreshed.

`nodes(id)` still returns `const std::vector<L>&`. Its views are cached for clean
queries. References returned by node, analysis, or index queries may be
invalidated by any graph mutation; copy values needed across `add()`/`merge()`.
Retired arena handles and union-find slots remain allocated so IDs stay valid.
Rebuilding does not reclaim all historical storage.

`check_invariants()` performs an explicit full check of node uniqueness, memo
entries, canonical children, dependencies, cached class membership, operator
membership, and graph counts. It requires a clean graph and is intended for
validation, not for every optimization iteration.

## Analysis modification

An analysis may optionally provide:

```cpp
void modify(eggc::EGraph<Node, MyAnalysis>& graph, eggc::Id id);
```

The hook is queued when a class is created, merged, or gains analysis facts.
It runs after current congruence and analysis work. Read `analysis_data(id)` and
use `find()`, `add()`, and `merge()` to materialize proven equations. Clean-only
queries are unavailable while rebuilding. Hooks must be idempotent and their
unbounded closure must converge. Calling `rebuild()` recursively is rejected.

For an arithmetic analysis whose `Data` is `std::optional<int>`, the following
hook inserts the inferred constant:

```cpp
void modify(Graph& graph, eggc::Id id) {
    auto value = graph.analysis_data(id); // Copy before add() can reallocate.
    if (value) {
        auto literal = graph.add(Node::leaf(std::to_string(*value)));
        graph.merge(id, literal);
    }
}
```

The analysis's `make()` must infer arithmetic using operand facts and checked
arithmetic; `SymbolLang` itself has no built-in arithmetic semantics. The
[advanced regression test](../tests/advanced_engine_test.cpp) supplies a complete
checked constant-folding analysis and optimizes `(+ (+ 2 3) 4)` to `9`.

Runner limits also apply between modification hooks. One callback may exceed
a limit before returning. On cancellation, rebuilding suspends further hooks
and drains structural/analysis repair so the graph can still be queried. Pending
hooks resume on a later `rebuild()` or `run()` with sufficient limits. A clean
graph means its structural and analysis invariants hold; after cancellation,
modification closure may still be pending. A plain `rebuild()` has no budget.
Analyses themselves must converge, since repair must finish before clean queries
are possible.

Advanced callers may use `graph.rebuild(should_stop)`, which returns `false` when
cancellation was observed. Callback exceptions leave the graph dirty and retain
pending work for retry. Rejection of a union or an exception does not roll back
previous graph changes from an entire hook or rebuild.

## Compiled matching

`CompiledPattern<L>` owns a validated pattern snapshot. Include
`eggc/matcher.hpp`, `eggc/core.hpp`, or `eggc/all.hpp` to use it:

```cpp
using Node = eggc::SymbolLang;
eggc::CompiledPattern<Node> pattern(eggc::parse_pattern("(pair ?x ?x)"));
pattern.search(graph, root, [&](const std::vector<eggc::Id>& bindings) {
    auto subst = pattern.substitution(bindings);
    return true; // false cancels enumeration
});
```

Variables have integer slots in `pattern.variables()` order. Matching uses
registers, an undo trail, explicit backtracking, and binding-vector deduplication.
More structured children are searched first. The runner compiles rules once
per invocation, freezes rules and hooks, and stores ordinary pending
applications as rule index, target, and numeric bindings. String substitutions
are constructed for conditions and public callbacks that request them. Custom
searchers retain their existing callback API.

Languages may opt into bound-subexpression memo lookups with
`static constexpr bool exact_matches = true`. This promises that `matches()`
compares exactly the node's non-child identity. `SymbolLang` opts in. A language
with broader attribute predicates must leave this flag absent or set it to
`false`, including when inheriting from `SymbolLang`. General predicates use
candidate enumeration so their matching semantics are preserved.

Compiled search programs are limited to 100,000 instructions and throw
`std::length_error` if compilation exceeds that budget. DAG pattern sharing is
legal but instruction compilation unfolds occurrences. Cost and callback
functions must not mutate the graph during a search or extraction.

## Cost types and extraction

Existing `Extractor<Node>` and `CostPolicy<Node>` calls retain AST-size defaults.
Use a third extractor template argument for another cost type:

```cpp
eggc::CostPolicy<Node, double> latency =
    [](const Node&, const std::vector<double>& child_costs)
        -> std::optional<double> {
        double result = 0.5;
        for (auto child : child_costs) result += child;
        return result;
    };
auto [cost, expression] =
    eggc::Extractor<Node, eggc::NoAnalysis<Node>, double>(graph, latency)
        .find_best(root);
```

A non-default cost type requires an explicit policy. Costs must be copyable,
consistently ordered by `<`, deterministic, monotone, and strictly greater than
each child cost. Lexicographic pairs are supported. Floating-point costs must
be finite; NaN and infinities are rejected. `nullopt` excludes an unavailable
node or an overflowed cost. Extraction propagates improved costs through parent
dependencies rather than repeatedly scanning the whole graph. Graph mutation
invalidates an existing extractor.

Tree cost counts repeated children repeatedly even though `RecExpr` preserves
sharing. For a DAG objective, use a separate extractor:

```cpp
eggc::DagOptions options;
options.state_limit = 100000;
auto result = eggc::DagExtractor<Node>(graph).solve(root, options);
if (result.cost) {
    // result.expression is the best finite DAG found.
    // Only result.optimal guarantees exhaustive optimization.
}
```

The default objective counts each selected e-class once. A custom optional
`double(const Node&)` policy supplies finite, nonnegative node weights; `nullopt`
excludes a node. One representative is selected per reached class, with explicit
cycle rejection. This is a dependency-free exact branch-and-bound search for
small graphs, not an ILP backend. Worst-case work is exponential. State and
optional time budgets return an incumbent marked non-optimal, or no cost if no
finite candidate was found. Exhaustion with no finite DAG throws. A budget limits
explored states, not total frontier allocation; one expansion/cost callback can
exceed a time budget. This interface leaves room for a solver backend later.

For `(f x x x)` equivalent to `(g (g x))`, tree extraction chooses the latter
at cost 3. DAG extraction chooses `(f x x x)` at cost 2 because `x` is shared.

## Multi-pattern joins

`MultiPattern<L>` is a conjunction of root-variable/pattern clauses with shared
bindings:

```cpp
eggc::MultiPattern<Node> joined({
    {"?f", eggc::parse_pattern("(f ?x ?y)")},
    {"?g", eggc::parse_pattern("(g ?x ?y)")},
});
auto matches = joined.match(graph);
auto rule = eggc::multi_rewrite<Node>(
    "joined", joined, "?f", eggc::parse_pattern("?x"));
```

Only combinations satisfying both clauses and all shared-variable equalities
are emitted. The search orders clauses by candidate count, seeds existing
bindings into compiled matching, deduplicates complete substitutions, and
honors cancellation. `multi_rewrite` validates its target/RHS variables and
replaces one bound root. For applications that modify several roots, use the
existing custom searcher/application API. Joining many large candidate sets can
still be expensive; use runner match/time limits.

## Iteration hooks and lookup

Iteration hooks run before search, observe a clean graph, and can add terms or
merge classes. The runner rebuilds between hooks. A false return requests
`StopReason::UserRequested` and leaves the graph queryable:

```cpp
std::vector<eggc::IterationHook<Node>> hooks{
    [](eggc::EGraph<Node>& graph, const eggc::RunReport& report) {
        return report.iterations < 5;
    },
};
auto report = eggc::run(graph, rules, eggc::RunOptions{}, hooks);
```

Mutations by hooks prevent premature saturation. Hooks are snapshotted at run
start and obey the same cooperative time/node limits as other callbacks.

`lookup(node)` canonicalizes children and returns an optional canonical class.
`lookup_expr(expr)` validates the bottom-up expression and looks it up without
inserting missing terms. Both require a clean graph. To explain two terms later,
retain their original handles from `add()`/`add_expr()`; looking up already-merged
terms returns their shared canonical class.

## Equality provenance and DOT

Call `enable_explanations()` before adding any terms. The graph records each
successful union with its endpoints and justification. The runner names rewrite
unions, congruence repair records child-equality premises, and modification hooks
are tagged as analysis unions. Direct callers can supply a named justification:

```cpp
graph.merge(a, b, {eggc::UnionKind::User, "application equation", {}});
graph.rebuild();
auto steps = graph.explain_equivalence(a, b);
```

The returned steps form a directed union path between the original handles.
Congruence premises can be explained separately through the same API. This is
optional equality provenance for debugging: user equations, rewrite semantics,
and analysis facts are trusted. It is not a semantic proof checker or a replay
of rewrite substitutions. Non-equivalent endpoints and explanations enabled
late are rejected.

`to_dot(graph)` produces Graphviz text using `LanguageIO<L>::format_op` and
escapes labels. Custom languages can use `to_dot(graph, formatter)` without a
text adapter. Child positions label edges; cycles and sharing remain visible.

## Installed CMake package

```sh
cmake -S . -B /tmp/eggc-package -DBUILD_TESTING=OFF -DEGGC_BUILD_EXAMPLES=OFF
cmake --build /tmp/eggc-package
cmake --install /tmp/eggc-package --prefix /tmp/eggc-install
```

A separate application can configure with `-DCMAKE_PREFIX_PATH=/tmp/eggc-install`
and use:

```cmake
find_package(eggc CONFIG REQUIRED)
target_link_libraries(my_application PRIVATE eggc::eggc)
```

The exported target supplies C++20 and the installed include directory. All
public headers and their implementation headers are installed. The package
consumer CTest verifies installation, discovery, compilation, and execution in
a separate project. Bazel consumers retain the header-only library target.
