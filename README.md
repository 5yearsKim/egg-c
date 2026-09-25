# egg-c

A C++17 implementation of e-graphs and equality saturation, inspired by Rust
[`egg`](https://github.com/egraphs-good/egg).

## Build and test

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The C++ versions of the Rust `egg` tutorials are documented in
[getting started](docs/tutorial_getting_started.md) and
[equivalences and unsafe rewrites](docs/tutorial_explanations.md).

## Lint and format

Install `clang-format` and `clang-tidy`, then configure the project as usual.
The `format` target applies clang-format to the C++ sources and headers; `tidy`
runs clang-tidy using the build's compile database. The `lint` target runs the
available tools:

```sh
cmake --build build --target lint
```

You can also run either tool separately with `--target format` or `--target tidy`.
Lint targets are enabled by default and can be disabled with
`-DEGGC_ENABLE_LINT=OFF`. CMake reports any missing tools during configuration;
install them and reconfigure to run all checks.

## Parse, saturate, and extract

```cpp
#include "eggc/constant_analysis.hpp"
#include "eggc/parser.hpp"
#include "eggc/extract.hpp"
#include "eggc/runner.hpp"

eggc::EGraph graph(std::make_shared<eggc::ConstantAnalysis>());
const auto root = graph.add_expr(eggc::parse_expr("(+ (* 2 3) (+ x 0))"));
graph.rebuild();

const auto rules = std::vector<eggc::Rewrite>{
    eggc::parse_rewrite("add-zero", "(+ ?x 0)", "?x")
};
const auto report = eggc::run(graph, rules);
const auto best = eggc::Extractor(graph).find_best_rec_expr(root);
std::cout << eggc::to_string(best.second) << " cost=" << best.first << '\n';
```

The constant analysis folds `+` and `*` over signed 64-bit literals. Overflowing
folds remain unknown. Expression and pattern syntax use S-expressions; quoted
atoms support `\\n`, `\\r`, `\\t`, `\\\\`, and `\\\"` escapes. Pattern atoms
starting with `?` are variables.

`RunOptions::per_rule_match_limit` enables per-rule exponential backoff. A rule
that exceeds its current match budget is deferred for the rest of that iteration
and retried with a doubled budget in the next iteration. This is optional and
disabled by default; the separate global `match_limit` still stops the run if
the total collected matches exceed its limit.

## Conditional rewrites

Conditions inspect a rebuilt graph during the runner's search phase. A match
rejected by a condition creates no RHS nodes. If an analysis fact changes during
rebuilding, the runner checks the condition again in the next iteration.

```cpp
eggc::EGraph graph(std::make_shared<eggc::ConstantAnalysis>());
const auto root = graph.add_expr(eggc::parse_expr("(/ 7 7)"));
const auto div_self = eggc::parse_rewrite(
    "div-self", "(/ ?x ?x)", "1", eggc::known_nonzero("?x"));
eggc::run(graph, {div_self});
// root is now equivalent to the literal 1.
```

`known_nonzero` accepts only an exact, nonzero signed 64-bit constant fact.
Zero, unknown values, and overflowing folds do not satisfy it. A missing or
incompatible analysis is a configuration error. The example assumes integer
division, for which division by zero is undefined; the constant analysis does
not evaluate division itself. Conditions must be read-only and remain true as
sound equalities and facts accumulate. `IterationStats::matches` counts distinct
structural matches, including those rejected by a condition; the report also
records `condition_checks` and `condition_rejections`.

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
`cost`, `fact`, `witness`, `match`, `rule`, and `run`. Query output compares equivalence,
normalized pattern bindings, extraction costs, and whether an extracted term
belongs to its requested class. Internal IDs, expression choices on ties, and
iteration counts are not compared. The arithmetic generator also checks the
expected truth of the guarded `x / x = 1` equality independently of Rust egg.

## Benchmarks

Configure with `-DEGGC_BUILD_BENCHMARKS=ON`, build, then run
`./build/eggc_bench [scale]`. It prints CSV timing rows for worklist and
full-scan deep congruence, many unrelated classes, and multi-match workloads.
Use a Release build and repeat runs before drawing performance conclusions.

The incremental rebuild is checked against an all-pairs full-scan reference
using deterministic randomized add/merge sequences in the normal test suite.
The Rust differential test may fetch pinned crate dependencies on its first run.
