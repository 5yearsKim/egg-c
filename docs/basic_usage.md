# Practical usage

[All guides](README.md) · Previous: [Getting started](tutorial_getting_started.md) · Next: [Equivalences and unsafe rewrites](tutorial_explanations.md)

This guide builds on the expression, e-class, and pattern concepts in
[Getting started](tutorial_getting_started.md). You will combine rewrite rules
with constant folding, add a condition to a rule, and inspect the runner's result.

## A complete constant-folding program

Save this as `examples/my_simplifier.cpp` in your local checkout:

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

From the repository root, configure, build, and run it:

```sh
cmake -S . -B build -DEGGC_BUILD_EXAMPLES=ON
cmake --build build --target my_simplifier
./build/examples/my_simplifier
```

CMake creates an example target for each `.cpp` file in `examples/`; rerunning
configuration picks up your new file. With a multi-configuration generator, add
`--config Debug` to the build command and use the executable in
`build/examples/Debug/` (`my_simplifier.exe` on Windows).

Expected output:

```text
(+ 6 x) cost=3
saturated: yes
```

There are two sources of equality here. `ConstantAnalysis` discovers that
`(* 2 3)` equals `6`, and `add-zero` makes `(+ x 0)` equivalent to `x`.
Rebuilding propagates those alternatives to the outer sum, so extraction can
choose `(+ 6 x)`. Its three tree nodes are `+`, `6`, and `x`.

An **analysis** attaches facts to e-classes and updates them as equalities
accumulate. This constant analysis also adds a literal expression when it knows
a class's value. You do not need a separate rewrite for every pair of numbers.
`run` rebuilds the graph for you; if you manually add or merge nodes and then
query the graph, call `rebuild()` first.

### What constant analysis knows

`ConstantAnalysis` recognizes signed 64-bit integer literals and folds binary
`+` and `*` when both operands have known constant values. It uses exact integer
results that fit in `int64_t`. An overflowing calculation remains unknown; it
is not folded using wraparound arithmetic.

| Expression | Fact from constant analysis alone |
| --- | --- |
| `(* 2 3)` | Known value `6`. |
| `(+ (* 2 3) 4)` | Known value `10`. |
| `(+ x 0)` | Unknown; a rewrite can still simplify it to `x`. |
| `(/ 6 3)` | Unknown; division is not evaluated by this analysis. |
| `(+ 9223372036854775807 1)` | Unknown because the result exceeds `int64_t`. |

A plain `eggc::EGraph graph;` has no analysis. It can still apply symbolic
rewrites, but it will not automatically fold `(* 2 3)` to `6`.

## Write patterns and rewrites

Patterns use the same parenthesized syntax as expressions, with `?`-prefixed
atoms acting as variables:

| Pattern | What it matches |
| --- | --- |
| `(+ ?x 0)` | A sum with any left operand and literal `0` on the right. |
| `(+ x 0)` | A sum with literal name `x` on the left and `0` on the right. |
| `(* ?x ?x)` | A product whose children belong to the same e-class. |
| `(* ?x ?y)` | A product with any two children, including equal ones. |

A rewrite searches from left to right and records equality between its matched
expression and its instantiated right-hand side. That equality is symmetric once
recorded, but the runner only searches the direction you wrote. For example,
`(+ ?x 0) -> ?x` does not generate sums from every leaf in the graph.

Every variable on the right must occur on the left. This is valid:

```cpp
const auto rule = eggc::parse_rewrite("add-zero", "(+ ?x 0)", "?x");
```

Using `?y` as that right-hand side would throw `std::invalid_argument`, because
there is no binding telling the runner what `?y` means. A condition's required
variables must also be bound by the left-hand pattern.

Operator names and child counts must match the pattern. The library does not
implicitly make operators commutative or associative; add the relevant rules
when they are valid for your language.

### Quoted atoms

Quote an atom when its name contains spaces or parentheses. This example uses a
C++ raw string literal so the expression's quotes do not need C++ escaping:

```cpp
const auto expr = eggc::parse_expr(R"((label "hello world"))");
// One operator, label, with one child named hello world.
```

Within quoted atoms, the expression parser supports `\n`, `\r`, `\t`, `\\`, and
`\"` escapes. Quoting does not disable pattern-variable recognition: when parsed
as a pattern, an atom whose name begins with `?` is still a variable.

## Conditional rewrites

Some identities need a precondition. For integer division, `x / x = 1` is valid
when `x` is nonzero. Here is a complete program you can put in
`examples/my_guarded_rule.cpp` and build with the target `my_guarded_rule` using
the same CMake steps as above:

```cpp
#include "eggc/constant_analysis.hpp"
#include "eggc/extract.hpp"
#include "eggc/parser.hpp"
#include "eggc/runner.hpp"
#include <iostream>
#include <memory>

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

Expected output is `1 cost=1`. The repeated `?x` requires equal operands; the
condition additionally requires an exact, known nonzero constant fact for that
class. This rule supplies the division equality even though the analysis itself
does not evaluate division.

Try changing just the input:

| Input | Extracted result | Why |
| --- | --- | --- |
| `(/ 7 7)` | `1` | The common operand is known to be nonzero. |
| `(/ 0 0)` | `(/ 0 0)` | The condition rejects zero. |
| `(/ x x)` | `(/ x x)` | An unknown value is not evidence of being nonzero. |
| `(/ (+ 2 3) 5)` | `1` | Rebuilding discovers that both operands equal `5`. |

`known_nonzero` requires a graph with `ConstantAnalysis`; a missing or incompatible
analysis is a configuration error. Overflowing folds also cannot establish its
nonzero precondition.

Conditions run against a rebuilt graph during search. A rejected match adds no
right-hand-side nodes. If analysis facts change during rebuilding, the condition
is checked again when that match is found in a later iteration. Custom conditions
must be read-only and must stay true as sound equalities and facts accumulate.
For example, accepting a match merely because two classes are currently different
would be unsuitable: a later merge could invalidate that assumption.

## Control the run and read its report

The default run allows 10 iterations and 10,000 stored nodes. Time and match
limits are unset by default. To choose your own limits, replace the `run` call in
the first program with the following block and add `#include <chrono>`:

```cpp
eggc::RunOptions options;
options.iteration_limit = 30;
options.node_limit = 50000;
options.time_limit = std::chrono::milliseconds(500);
options.match_limit = 100000;
const auto report = eggc::run(graph, rules, options);
```

These are example budgets, not requirements. Check `report.reason` before
assuming that saturation was reached:

| Stop reason | Meaning |
| --- | --- |
| `Saturated` | A completed iteration made no graph change and no rule was deferred by backoff. |
| `IterationLimit` | The allowed iterations were exhausted. |
| `NodeLimit` | The graph reached or exceeded its node budget. |
| `TimeLimit` | A cooperative time check reached the deadline. |
| `MatchLimit` | Search encountered more structural matches than allowed for that iteration. Its pending applications were discarded. |

A limited run still leaves a rebuilt graph from which you can extract a result.
That result is the cheapest represented expression under your cost policy;
additional iterations or different rules may discover better alternatives.
Even saturation only describes what your supplied rules can discover.

Limits are checked at specific points in the runner. A rewrite application or
rebuild can take the node count past the budget. Time checks are cooperative, so
a single application or rebuild can finish after the requested deadline.

`report.history` contains per-iteration statistics. For example, place this after
`run` to see whether conditions are rejecting matches:

```cpp
for (const auto& step : report.history) {
    std::cout << "matches=" << step.matches
              << " condition checks=" << step.condition_checks
              << " rejected=" << step.condition_rejections
              << " nodes=" << step.nodes << '\n';
}
```

`matches` includes distinct structural matches rejected by conditions.
`applications` counts applied matches, which may already be known equalities;
`rewrite_unions` counts actual class merges caused by those applications.
`report.iterations` includes started partial iterations, and each history entry's
`completed` field tells you whether application finished for that iteration.

For rules that generate many matches, optional `per_rule_match_limit` enables
exponential backoff. Set it to a positive initial budget. If a rule has more
matches, search defers its remaining matches for that iteration and doubles its
budget for the next iteration. Matches already collected for that rule can still
be applied. Once a rule completes search within its budget, its budget resets to
the initial value. The separate global `match_limit` can still stop the run.

## Choose an extraction cost

The default `ast_size_cost()` counts every occurrence in the expression tree.
For example, `(+ x x)` costs 3 even though both children refer to the same class.
`RecExpr` can share nodes in storage, so its stored node count can differ from
this cost.

For the smallest tree depth instead, use:

```cpp
const auto [depth, shallowest] =
    eggc::Extractor(graph, eggc::ast_depth_cost()).find_best_rec_expr(root);
```

A leaf has depth 1; `(+ 6 x)` has depth 2. Choosing size or depth can favor
different expressions. Neither policy automatically models runtime performance.
On cost ties, do not rely on a particular printed expression.

Create a new extractor after modifying the graph. An extractor caches choices
for the graph revision at construction; querying it after the graph changes
throws an error. Advanced custom cost policies must be deterministic,
nondecreasing in child costs, and return a cost strictly greater than every
child's cost. See [`extract.hpp`](../include/eggc/extract.hpp) for the interface.

## Troubleshooting

| Symptom | What to check |
| --- | --- |
| No tutorial or custom example target | Configure with `-DEGGC_BUILD_EXAMPLES=ON`; put custom `.cpp` files in `examples/` and rerun configuration. |
| Cannot find the executable | Check `build/examples/`, or its configuration subdirectory such as `Debug/`. Windows executables end in `.exe`. |
| `query requires a rebuilt e-graph` | Call `rebuild()` after manual additions or merges, before matching or extraction. |
| `extractor graph changed after extractor construction` | Rebuild the graph and construct a new extractor. |
| `unbound rhs variable` | Bind every right-hand-side variable in the left-hand pattern. |
| `known-nonzero condition requires ConstantAnalysis` | Construct the graph with `std::make_shared<eggc::ConstantAnalysis>()`. |
| An expected rule does not match | Check operator spelling, child count, operand order, repeated variables, and conditions. |
| Arithmetic stays symbolic | A plain graph has no constant folding; `ConstantAnalysis` only evaluates binary `+` and `*`. |
| The run stops before saturation | Inspect `report.reason` and the history. Look for rules that keep generating new expressions before increasing budgets. |
| `AnalysisConflict` | Your merges or rules have forced incompatible facts into one class, such as two different integer constants. Revisit those equalities. |

## Try it yourself

1. In the first program, remove `add-zero` but keep constant analysis. What is
   the cheapest expression now?
2. Keep `add-zero`, but use a plain `EGraph` instead. What changes?
3. In the guarded program, try `(/ -3 -3)` and `(/ x x)`. Why do they differ?

<details>
<summary>Answers</summary>

1. `(+ 6 (+ x 0))`, with AST size 5. Analysis folds the product but cannot give
   the unknown sum a constant value.
2. `(+ (* 2 3) x)`, with AST size 5. The rewrite simplifies `(+ x 0)`, but
   nothing evaluates `(* 2 3)`.
3. The first becomes `1` because `-3` is known and nonzero. The second stays
   unchanged because the graph has no fact ruling out `x = 0`.

</details>

Continue to [Equivalences and unsafe rewrites](tutorial_explanations.md) to see
how an unguarded division rule can make a graph report `0 = 1`. The remaining
sections describe optional tools for working on the library itself.

## Differential verification against Rust egg

Enable the optional test to replay the same operation streams through this
library and pinned Rust `egg` 0.9.5. Cargo uses the checked-in lockfile:

```sh
cmake -S . -B build -DEGGC_ENABLE_EGG_DIFFERENTIAL=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The normal differential test runs four saved fixtures plus 20 seeded graph and
20 seeded arithmetic cases. For a larger run or a specific seed:

```sh
python3 tests/differential/check.py build/eggc_differential cargo \
  tests/differential/rust_egg --cases 1000 --seed 0xEE77
```

On a mismatch, the driver writes `differential_failure.case`, both outputs,
and a smaller `.min.case` when reduction succeeds. Replay the saved stream with
`--replay differential_failure.case`. Case files are tab-separated and start
with `version<TAB>1`; supported operations are `add`, `merge`, `rebuild`, `eq`,
`cost`, `fact`, `witness`, `match`, `rule`, and `run`. Query output compares
equivalence, normalized pattern bindings, extraction costs, and whether an
extracted term belongs to its requested class. Internal IDs, expression
choices on ties, and iteration counts are not compared. The arithmetic
generator also checks the expected truth of the guarded `x / x = 1` equality
independently of Rust egg.

## Benchmarks

Configure with `-DEGGC_BUILD_BENCHMARKS=ON`, build, then run
`./build/eggc_bench [scale]`. It prints CSV timing rows for worklist and
full-scan deep congruence, many unrelated classes, and multi-match workloads.
Use a Release build and repeat runs before drawing performance conclusions.

The incremental rebuild is checked against an all-pairs full-scan reference
using deterministic randomized add/merge sequences in the normal test suite.
The Rust differential test may fetch pinned crate dependencies on its first run.
