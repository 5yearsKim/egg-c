# egg-c

A header-only C++20 e-graph library inspired by [egg](https://github.com/egraphs-good/egg).
Use the supplied `SymbolLang` to get started, or provide a custom node type and
analysis for your application.

```cpp
#include <eggc/all.hpp>
#include <iostream>
#include <vector>

int main() {
    using eggc::rewrite;
    auto input = eggc::parse_expr("(+ 0 (* 1 a))");
    std::vector rules{
        rewrite("commute-add", "(+ ?x ?y)", "(+ ?y ?x)"),
        rewrite("add-zero", "(+ ?x 0)", "?x"),
        rewrite("mul-one", "(* 1 ?x)", "?x"),
    };

    eggc::EGraph<eggc::SymbolLang> graph;
    auto root = graph.add_expr(input);
    auto report = eggc::run(graph, rules);
    if (report.reason != eggc::StopReason::Saturated) return 1;

    auto [cost, best] = eggc::Extractor<eggc::SymbolLang>(graph).find_best(root);
    std::cout << eggc::to_string(best) << '\n'; // a
}
```

`SymbolLang` stores symbols, including `0` and `1`; it does not perform arithmetic
by itself. Rewrite rules supply equations. Custom languages can store typed
values and attributes, with optional parsing through `LanguageIO<L>`.
Conditional rules use `Condition<L, A>` to inspect matches and analysis facts.

## Build and run

```sh
cmake -S . -B /tmp/eggc-build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/eggc-build
ctest --test-dir /tmp/eggc-build --output-on-failure
/tmp/eggc-build/examples/tutorial_getting_started
```

Or use Bazel:

```sh
bazel test //tests:all //examples:all
bazel run //examples:tutorial_getting_started
```

Examples build by default; use `-DEGGC_BUILD_EXAMPLES=OFF` to omit them. CMake
consumers link the `eggc` interface target, which supplies the include path and
C++20 requirement. External Bazel consumers must enable C++20 in their workspace.
There is no compiled library to link.

## Learn

- [Getting started](docs/tutorial_getting_started.md): expressions, patterns, and optimization.
- [Custom languages](docs/custom_languages.md): typed nodes and parsing.
- [Conditional rewrites](docs/conditional_rewrites.md): rules that require proven facts.
- [Code structure](docs/architecture.md): header responsibilities and a reading order.
- [Advanced features](docs/advanced_features.md): analysis hooks, matching, extraction, and installed packages.
- [Performance and diagnostics](docs/performance.md): benchmarks and engine statistics.
- [Runnable examples](examples/): all examples also run as tests.

## Layout

```text
include/eggc/   Public API headers
  impl/        Included template implementations and text syntax helpers
examples/      Runnable examples
tests/         Engine and API regression tests
docs/          Short guides
misc/images/   Artwork
```

Use `eggc/all.hpp` for the complete library, `eggc/core.hpp` for the generic
engine, or `eggc/text.hpp` for symbols and text-based rules. Individual public
headers also work. Template definitions live in `impl/` and are included
automatically; ship that directory with the headers. The library remains
header-only, with no `src/` directory.

Use `eggc/engine.hpp` for the minimal generic engine; `core.hpp` and `all.hpp`
remain compatibility umbrellas. The [reusable runner example](examples/reusable_runner.cpp)
shows compiled rule reuse, execution slices, scoped extraction, and rewrite replay.
