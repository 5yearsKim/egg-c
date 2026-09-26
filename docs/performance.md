# Performance and engine diagnostics

## Run the benchmarks

Build with optimization; Debug timings are not comparable to Release timings.
Benchmarks are optional and require no additional libraries.

```sh
cmake -S . -B /tmp/eggc-perf -DCMAKE_BUILD_TYPE=Release -DEGGC_BUILD_BENCHMARKS=ON
cmake --build /tmp/eggc-perf --parallel 2
/tmp/eggc-perf/benchmarks/engine_benchmark all 100 3 > /tmp/eggc-results.csv
```

Or use Bazel:

```sh
bazel run --config=release //benchmarks:engine_benchmark -- all 100 3
```

The command accepts `[workload|all] [size] [repetitions]`. Each sample starts
with a fresh graph. The inputs are deterministic. A smoke test runs all six
workloads at size 8; it checks execution without imposing timing thresholds.

| Workload | Input size | Measured operation |
| --- | --- | --- |
| `analysis_chain` | Dependency chain length | Propagate a new fact against class traversal order |
| `congruence_cascade` | Depth of two unary chains | Merge their leaves and repair congruence |
| `repeated_variables` | Number of paired inputs | Match and apply `(pair ?x ?x) -> ?x` |
| `rewrite_growth` | Number of nested arithmetic terms | Four iterations of commutativity, associativity, distributivity, and identity rules |
| `shared_expression` | Binary DAG depth | Build, rebuild, and extract a graph with repeated children |
| `small_update` | Number of unrelated components | Rebuild after changing one small component |

`shared_expression` is limited to depth 30 to keep AST-size costs finite on
supported platforms. In `all` mode that workload is capped at 30. Rewrite
workloads use a 100,000-node limit and a 10,000-match limit per iteration;
the CSV records the actual stop reason. Match limits are cooperative, not
hard bounds on every intermediate matching operation.

## Read the CSV

Each row records workload, input size, repetition, final node/class counts,
analysis evaluations, started iterations, inspected matches, applications,
stop reason, elapsed nanoseconds, search/apply/rebuild nanoseconds, extraction
nanoseconds, and extracted AST cost.

Graph construction and preparation are outside `elapsed_ns`. Extraction is
also timed separately. For workloads that only rebuild, runner phase timings
are zero and `elapsed_ns` is the rebuild time. For rewrite workloads it is the
whole runner time, including initial rebuilding; the individual phase columns
sum iteration work and exclude the initial rebuild.

An analysis evaluation is a call to the benchmark analysis's `make()` during
the measured operation. It excludes setup and counts every node evaluation,
including those that do not change facts. AST cost measures the unfolded tree;
the extracted `RecExpr` still preserves sharing.

Compare operation counts and final results before comparing times. Use the
same compiler, optimization flags, machine, workload, and limits for timing
comparisons. Report several samples; do not make CI depend on wall-clock
thresholds. The bounded rewrite workload may stop before saturation.

## Rebuild counters

`graph.last_rebuild_stats()` exposes the most recent rebuild's
`congruence_passes`, `congruence_unions`, `analysis_evaluations`, and
`analysis_changes`, `analysis_unions`, `repaired_nodes`, `refreshed_classes`, and
`modifications`. Calling `rebuild()` on a clean graph resets these counters
to zero without changing the graph revision. Counters also describe completed
work when a rebuild throws; they do not imply that the graph is clean.

`RunReport::initial_rebuild` records the runner's initial rebuild, while each
iteration records `analysis_evaluations` for its subsequent rebuild. The
existing iteration `analysis_changes` field counts analysis revision events
throughout that iteration, including node additions and union-related updates;
it is not just the rebuild's changed-fact count.

## Per-rule diagnostics

```cpp
eggc::RunOptions options;
options.collect_rule_stats = true;
options.per_rule_match_limit = 1000;
auto report = eggc::run(graph, rules, options);
for (const auto& iteration : report.history) {
    for (const auto& rule : iteration.rules) {
        std::cout << rule.rule_index << ' ' << rule.name << ' '
                  << rule.matches << ' ' << rule.applications << '\n';
    }
}
```

Each entry records its rule index and name, matches, condition checks and
rejections, applications, rewrite unions, search/application times, and
`searched`, `search_completed`, and `backed_off` flags. Duplicate names remain
distinct by index. When a search limit stops an iteration, later rules have
`searched == false`. Diagnostics are disabled by default; `iteration.rules`
is empty in that case.

Exceeding a rule's match budget discards every application queued by that rule
in the iteration, then doubles its budget for the next iteration. Other rules'
applications remain queued. Matches already inspected by the deferred rule
still count toward the global match limit, including rejected conditions.
The extra match used to detect an exceeded budget is not included in match
counters and its built-in condition is not evaluated. Exceeding the global
match limit discards all applications queued in the iteration. Deferred rules
prevent an unchanged iteration from being reported as saturated.

## Measured improvements

At reverse-chain length 400, the original repeated whole-graph algorithm used
321,602 analysis evaluations. The intermediate dependency-queue implementation
used 1,201. Persistent dependencies now use 401 for the same graph and fact;
a second clean rebuild performs zero evaluations.

With 2,000 unrelated components, `small_update` used 4,003 evaluations before
persistent rebuilding and now uses 2. Final graph counts and extracted cost
remain the same. These are operation counts, not a comparison of Debug and
Release timings. Regression tests enforce bounded work on isolated updates and
compare randomized propagation with an independent fixed-point oracle.

Only affected node/class views and operator memberships are rebuilt. Candidate
vectors are now materialized lazily by the first query for each changed
operator. This defers copying; a query for an operator with many classes still
copies that operator's candidate list once. Use `prepare_indexes()` to warm
these caches before sharing the graph with concurrent readers.

Arena and union-find storage retain historical handles. `memory_stats()` makes
retained storage visible. Extraction without roots initializes all live
classes; the root-vector constructor restricts it to reachable alternatives.
Parent-use callbacks avoid rebuilding temporary parent-class sets.

## Allocation and resource diagnostics

```sh
/tmp/eggc-perf/benchmarks/advanced_benchmark all 2000 3
```

The optional `advanced_benchmark` provides three deterministic workloads:

| Workload | Measured work |
| --- | --- |
| `matching_reuse` | Match `(pair ?x ?x)` once per class with a warmed workspace |
| `wide_dag` | Explore one state of a class containing many alternatives |
| `same_operator_update` | Rebuild after merging leaves whose parents share an operator with unrelated classes |

It records matches, cost calls, allocation requests, requested bytes,
materialized candidate IDs, and elapsed nanoseconds. Allocation interception is
benchmark-only and single-threaded; it reports requests in the measured window,
not peak or resident memory. Setup, workspace warmup, and invariant checks are
outside that window. The smoke test checks all three workloads at size 8.

Comparing the same benchmark source at size 2,000 before and after this pass,
compiled with Clang and `-std=c++20 -O2`, produced these deterministic counts:

| Work | Before | After |
| --- | ---: | ---: |
| Matcher allocation requests for 2,000 matches | 14,000 | 0 |
| Matcher requested bytes in the warmed search loop | 400,000 | 0 |
| DAG cost evaluations with `state_limit = 1` | 2,000 | 1 |
| DAG allocation requests | 10,014 | 8 |
| DAG requested bytes | 872,368 | 596 |

Zero allocations applies to this fixed pattern and one-result-per-class
workload after warmup. Larger results or patterns can grow buffers. A
same-operator update now materializes zero candidate IDs during rebuilding;
requesting that operator's index performs the deferred copy. The baseline had
no candidate-copy counter; its CSV marks that field `unavailable` when built
with `EGGC_REVIEW_BASELINE`. Timing samples are diagnostic, not CI thresholds.

See [advanced features](advanced_features.md) for the new matching, extraction,
hook, provenance, and packaging APIs and their limits.
