# Architecture and next steps

The library separates expression syntax, graph algorithms, and saturation:

- `expr.hpp` defines flat and tree expressions. `expr.cpp` parses and prints them with the same atom escaping rules.
- `pattern.hpp` defines patterns and matching. Pattern variables have an explicit kind; the parser interprets `?` syntax.
- `rewrite.hpp` defines rewrites and validates variable bindings. Parsing depends on this definition, not on the runner.
- `egraph.hpp` owns equivalence classes, congruence closure, and optional e-class analysis. `rebuild()` runs congruence, analysis, compaction, and index construction in that order until stable.
- `runner.hpp` coordinates search, application, rebuilding, limits, and reports. Search runs on a rebuilt graph before any rewrite is applied.
- `extract.hpp` selects the least-cost expression from a rebuilt graph.

An e-graph becomes dirty after `add()` or `merge()`. Call `rebuild()` before inspecting class nodes, searching patterns, querying the operator index, or extracting. Those operations reject dirty graphs. `find()`, `classes()`, counts, and analysis data remain available while dirty, but node counts reflect stored nodes and may change after compaction.

The full-scan rebuild reference lives in `tests/support/full_scan.cpp` and is linked only into tests and benchmarks. It checks incremental rebuild behavior without adding the reference algorithm to the main library.

Remaining work should follow measured need: profile rebuild and matching with the existing benchmarks, then consider more persistent parent-use indexes or compiled patterns if those are bottlenecks. The legacy tree extraction API remains for callers; the flat `RecExpr` API preserves shared subexpressions and handles deep expressions without recursive reconstruction.
