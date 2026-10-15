# egg-c guides

See [installation](installation.md) for build and integration instructions.
Start with [getting started](01_getting_started.md). It uses the supplied
symbol language and requires no custom node code.

- [Expressions and matching](02_expression_and_matching.md): search patterns and merge equivalent expressions.
- [Conditional rewrites](03_conditional_rewrites.md): use a condition to decide when a rule applies.
- [Custom languages](04_custom_langage.md): build a typed language for search filters.
- [Custom language reference](custom_languages.md): node contracts, text support, and analysis.
- [Code structure](architecture.md): navigate the implementation.
- [Public API overview](api.md): find the entry point for a task.
- [Reusable runners](reusable_runners.md): compile rules once and resume execution.
- [Advanced features](advanced_features.md): explanations, DAG extraction, and joined patterns.
- [Performance and diagnostics](performance.md): benchmarks, rebuild counters, and rule statistics.
- [Examples](https://github.com/5yearsKim/c-egg/tree/main/examples): runnable programs, checked by CMake and Bazel tests.

The [reusable runner example](https://github.com/5yearsKim/c-egg/blob/main/examples/reusable_runner.cpp) demonstrates
execution slices, scoped extraction, and rewrite verification.
