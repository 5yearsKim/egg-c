# egg-c guides

Start with [getting started](01_getting_started.md). It uses the supplied
symbol language and requires no custom node code.

- [Expressions and matching](02_expression_and_matching.md): search patterns and merge equivalent expressions.
- [Conditional rewrites](03_conditional_rewrites.md): use a condition to decide when a rule applies.
- [Custom languages](04_custom_langage.md): build a typed language for search filters.
- [Custom language reference](custom_languages.md): node contracts, text support, and analysis.
- [Code structure](architecture.md): navigate the implementation.
- [Advanced features](advanced_features.md): persistent rebuilding, hooks, DAGs, joins, and packages.
- [Performance and diagnostics](performance.md): benchmarks, rebuild counters, and rule statistics.
- [Examples](../examples/): runnable programs, checked by CMake and Bazel tests.

The [reusable runner example](../examples/reusable_runner.cpp) demonstrates the
execution and verification APIs introduced by the cleanup pass.
