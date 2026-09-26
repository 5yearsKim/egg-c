# Code structure

The public headers describe the API. `include/eggc/impl/` contains template
implementations and internal text helpers. Public headers include their own
implementations, so applications do not include files from `impl/`.

## Entry points

| Header | What it provides |
| --- | --- |
| `all.hpp` | Complete library; use this to get started |
| `core.hpp` | Generic e-graph engine for any application language |
| `text.hpp` | Symbol language, parsing, printing, and textual rewrites |

The engine uses node values and patterns. The optional text API converts strings
into those same values and patterns before optimization.

## Read the engine

Follow this order to understand how expressions become optimized results:

1. `language.hpp`, `id.hpp`: the node contract and child IDs.
2. `expr.hpp`, `egraph.hpp`: expression DAGs and equivalence classes.
3. `analysis.hpp`: facts shared by every member of a class.
4. `pattern.hpp`, `rewrite.hpp`: matches, replacements, and conditions.
5. `runner.hpp`: repeated search/application and stopping limits.
6. `extract.hpp`, `dag_extract.hpp`: tree and DAG extraction.
7. `matcher.hpp`, `multipattern.hpp`: compiled matching and joined clauses.
8. `dot.hpp`: graph visualization.

For an algorithm's implementation, open the corresponding `.tpp` in `impl/`.
The [custom-language regression tests](../tests/language_test.cpp) exercise the
engine independently of the text API.

## Read the text API

| File | Responsibility |
| --- | --- |
| `symbol_lang.hpp` | Supplied symbolic node and its text adapter |
| `language_io.hpp` | Optional conversion between operator tokens and custom nodes |
| `parser.hpp` | Public expression/pattern parsing functions; also includes printing |
| `printer.hpp` | Public node, e-graph, expression, and pattern printing functions |
| `parse_error.hpp` | Parsing diagnostics and source locations |
| `rewrite_text.hpp` | Parse and validate a named rewrite, with an optional condition |
| `impl/sexpr.hpp` | Tokenization, quoted symbols, and escapes |
| `impl/parser.tpp` | Build expression/pattern entries from tokens |
| `impl/printer.tpp` | Format nodes, e-graphs, expressions, and patterns as text |

## Examples, tests, and builds

`examples/` introduces symbols, custom languages, and conditions in that order.
`tests/` covers engine behavior, compile-time contracts, and text conversion.
Each directory owns its CMake and Bazel targets. Root build files expose the
header-only library and connect those targets.

`benchmarks/` contains six deterministic workloads. Enable it with
`EGGC_BUILD_BENCHMARKS=ON` in CMake or build its Bazel targets directly. CI checks
GCC, Clang, address/undefined-behavior sanitizers, and the Bazel build. See
[performance and diagnostics](performance.md) for commands and counters.

Keep an application-specific operator or analysis in the application. Put a
generic engine change in its public header and matching template implementation;
put syntax changes in the text files above. Add a focused regression test under
`tests/` and a runnable example when introducing a new user-facing workflow.

The graph's persistent node arena, parent-use lists, and work queues are in
`egraph.hpp`/`impl/egraph.tpp`. Clean class views retain the original public node
API. `impl/matcher.hpp` implements the numeric matching machine used by public
patterns and the runner. Extraction policies propagate costs through the graph's
parent dependencies. See [advanced features](advanced_features.md) for contracts
and limitations.
