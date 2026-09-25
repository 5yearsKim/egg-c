# Practical usage

[All guides](README.md) · Previous: [Getting started](tutorial_getting_started.md) · Next: [Equivalences and unsafe rewrites](tutorial_explanations.md)

## Constant folding and rewrites

Save this complete program as `examples/my_simplifier.cpp`:

```cpp
#include "eggc/constant_analysis.hpp"
#include "eggc/extract.hpp"
#include "eggc/parser.hpp"
#include "eggc/runner.hpp"
#include <iostream>
#include <memory>
#include <vector>

int main() {
    eggc::EGraph graph(std::make_shared<eggc::ConstantAnalysis>());
    const auto root = graph.add_expr(
        eggc::parse_expr("(+ (* 2 3) (+ x 0))"));

    const std::vector<eggc::Rewrite> rules{
        eggc::parse_rewrite("add-zero", "(+ ?x 0)", "?x")
    };
    const auto report = eggc::run(graph, rules);
    const auto [cost, best] =
        eggc::Extractor(graph).find_best_rec_expr(root);

    std::cout << eggc::to_string(best) << " cost=" << cost << '\n';
    std::cout << "saturated: "
              << (report.reason == eggc::StopReason::Saturated ? "yes" : "no")
              << '\n';
}
```

Build and run from the repository root:

```sh
cmake -S . -B build -DEGGC_BUILD_EXAMPLES=ON
cmake --build build --target my_simplifier
./build/examples/my_simplifier
```

CMake creates a target for each `.cpp` file in `examples/`. Rerun configuration
after adding a file. For multi-configuration generators, use `--config Debug`
and the executable under `build/examples/Debug/` (`.exe` on Windows).

Expected output:

```text
(+ 6 x) cost=3
saturated: yes
```

`ConstantAnalysis` folds `(* 2 3)` to `6`; `add-zero` makes `(+ x 0)` equivalent
to `x`. Extraction chooses `(+ 6 x)`, whose three tree nodes are `+`, `6`, and `x`.
An **analysis** attaches facts to classes and updates them as equalities accumulate.
This analysis also adds a literal when it knows a class's value.

Constant folding supports binary `+` and `*` over signed 64-bit integers.
Unknown operands and overflowing results stay unknown; division is not folded.
A plain `EGraph` has no constant analysis, so it only uses the equalities you add.

## Writing rules

A rewrite searches from left to right and records an equality. For example,
`(+ ?x 0) -> ?x` simplifies existing sums; it does not generate sums from leaves.
Every right-hand-side variable must appear on the left, or rule validation throws
`std::invalid_argument`. Conditions must also use variables bound on the left.

Matching respects operator names, child counts, and operand order. Add
commutativity or associativity rules explicitly when they are valid for your
language. Repeated variables require the same e-class at every occurrence.

For atom names containing spaces or parentheses, use quotes:

```cpp
const auto expr = eggc::parse_expr(R"((label "hello world"))");
// One operator, label, with one child named hello world.
```

Quoted atoms support `\n`, `\r`, `\t`, `\\`, and `\"` escapes. In patterns,
quoted names starting with `?` are still treated as variables.

## Conditional rewrites

For integer division, `x / x = 1` requires a nonzero operand. To try it, replace
`main` in the program above with:

```cpp
int main() {
    eggc::EGraph graph(std::make_shared<eggc::ConstantAnalysis>());
    const auto root = graph.add_expr(eggc::parse_expr("(/ 7 7)"));
    const auto div_self = eggc::parse_rewrite(
        "div-self", "(/ ?x ?x)", "1", eggc::known_nonzero("?x"));

    eggc::run(graph, {div_self});
    const auto [cost, best] =
        eggc::Extractor(graph).find_best_rec_expr(root);
    std::cout << eggc::to_string(best) << " cost=" << cost << '\n';
}
```

Expected output is `1 cost=1`. The repeated variable requires equal operands;
`known_nonzero` additionally requires an exact nonzero integer fact.

| Input | Extracted result | Reason |
| --- | --- | --- |
| `(/ 7 7)` | `1` | The common operand is known to be nonzero. |
| `(/ 0 0)` | `(/ 0 0)` | Zero fails the condition. |
| `(/ x x)` | `(/ x x)` | The value of `x` is unknown. |
| `(/ (+ 2 3) 5)` | `1` | Constant analysis makes both operands equal to `5`. |

`known_nonzero` requires `ConstantAnalysis`; missing or incompatible analysis is
an error. Conditions inspect a rebuilt graph during search, and rejected matches
add no replacement nodes. Custom conditions must be read-only and remain true
as sound equalities and facts accumulate.

## Control the run and read its report

The defaults allow 10 iterations and 10,000 nodes, with no time or match limit.
To set budgets, replace the first program's `run` call with this block and add
`#include <chrono>`:

```cpp
eggc::RunOptions options;
options.iteration_limit = 30;
options.node_limit = 50000;
options.time_limit = std::chrono::milliseconds(500);
options.match_limit = 100000;
const auto report = eggc::run(graph, rules, options);
```

Check `report.reason` to see why the run stopped:

| Stop reason | Meaning |
| --- | --- |
| `Saturated` | An iteration made no graph change and no rule was deferred. |
| `IterationLimit` | The iteration budget was exhausted. |
| `NodeLimit` | The graph reached or exceeded its node budget. |
| `TimeLimit` | A cooperative time check reached the deadline. |
| `MatchLimit` | Search exceeded its match budget; that iteration's pending applications were discarded. |

A limited run still leaves a rebuilt graph for extraction. Its best expression
is the cheapest currently represented; more rules or iterations may discover a
better one. Node and time limits may be exceeded by an application or rebuild.

`report.history` records per-iteration matches, applications, merges, node counts,
and timing. Matches include those rejected by conditions; `condition_rejections`
counts those rejections. See [`runner.hpp`](../include/eggc/runner.hpp) for all fields.

Optional `per_rule_match_limit` sets a positive initial match budget per rule.
Exceeding it defers the remaining matches and doubles that rule's budget for the
next iteration. Collected matches can still apply. The budget resets after a
complete search; the global `match_limit` can still stop the run.

## Choose an extraction cost

The default `ast_size_cost()` counts tree nodes, including repeated occurrences:
`(+ x x)` costs 3. To minimize tree depth instead:

```cpp
const auto [depth, shallowest] =
    eggc::Extractor(graph, eggc::ast_depth_cost()).find_best_rec_expr(root);
```

A leaf has depth 1; `(+ 6 x)` has depth 2. These costs describe expression shape,
not execution time, and ties may produce different printed expressions.

Create a new extractor after modifying and rebuilding the graph: its cached
choices belong to the revision at construction. Custom cost policies must be
deterministic, nondecreasing in child costs, and strictly greater than every child
cost. See [`extract.hpp`](../include/eggc/extract.hpp).

## Troubleshooting

| Symptom | What to check |
| --- | --- |
| Missing example target | Enable `EGGC_BUILD_EXAMPLES` and rerun configuration after adding a file. |
| Query requires a rebuilt graph | Call `rebuild()` after manual additions or merges. |
| Extractor reports a changed graph | Rebuild and construct a new extractor. |
| Unbound variable | Bind every replacement or condition variable in the left-hand pattern. |
| Rule does not match | Check operand order, operator spelling, child count, repeated variables, and conditions. |
| Arithmetic stays symbolic | Attach `ConstantAnalysis`; it folds only binary `+` and `*`. |
| `AnalysisConflict` | Check for rules or merges equating incompatible constants. |

**Try it:** remove `add-zero` from the first program. The result becomes
`(+ 6 (+ x 0))`, with cost 5: analysis still folds the product, but simplifying
the symbolic sum requires the rule.

Continue to [Equivalences and unsafe rewrites](tutorial_explanations.md).
The following tools are optional and intended for library development.

## Differential verification against Rust egg

This compares operation streams with pinned Rust `egg` 0.9.5. It requires Cargo
and Python 3; Cargo may fetch dependencies on its first run.

```sh
cmake -S . -B build -DEGGC_ENABLE_EGG_DIFFERENTIAL=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The test runs four saved fixtures plus 20 seeded graph and 20 seeded arithmetic
cases. For a larger run:

```sh
python3 tests/differential/check.py build/eggc_differential cargo \
  tests/differential/rust_egg --cases 1000 --seed 0xEE77
```

Failures produce `differential_failure.case`, both outputs, and a reduced
`.min.case` when possible. Add `--replay differential_failure.case` to replay a
failure. Comparisons cover equivalence, pattern bindings, extraction costs, and
whether extracted terms belong to their classes; IDs, tied expression choices,
and iteration counts are not compared. See the
[driver](../tests/differential/check.py) for the case format and options.

## Benchmarks

Configure with `-DEGGC_BUILD_BENCHMARKS=ON`, build, and run
`./build/eggc_bench [scale]`. It prints CSV timings for worklist and full-scan
rebuilding and matching workloads. Use a Release build and repeated runs when
comparing performance.
