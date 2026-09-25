# egg-c

A C++17 implementation of e-graphs and equality saturation, inspired by Rust
[`egg`](https://github.com/egraphs-good/egg).

## Build and test

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

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

## Benchmarks

Configure with `-DEGGC_BUILD_BENCHMARKS=ON`, build, then run
`./build/eggc_bench [scale]`. It prints CSV timing rows for worklist and
full-scan deep congruence, many unrelated classes, and multi-match workloads.
Use a Release build and repeat runs before drawing performance conclusions.

The incremental rebuild is checked against an all-pairs full-scan reference
using deterministic randomized add/merge sequences in the normal test suite.
An optional small semantic comparison with Rust `egg` 0.9.5 is available with
`-DEGGC_ENABLE_EGG_DIFFERENTIAL=ON`; CTest then invokes Cargo and may fetch the
pinned crate dependencies on its first run.
