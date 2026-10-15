# Public API overview

egg-c is a header-only library. This page maps common tasks to public entry
points. Start with `<eggc/all.hpp>` while learning, then include narrower
headers when needed.

## Choose an entry point

| Header | Contents |
| --- | --- |
| `eggc/all.hpp` | The complete library. |
| `eggc/engine.hpp` | The minimal generic engine for custom application languages. |
| `eggc/core.hpp` | A compatibility umbrella with optional graph tools. |
| `eggc/text.hpp` | The symbol language, parsing, printing, and text-based rules. |

## Expressions and equivalence

| API | Purpose |
| --- | --- |
| `SymbolLang` | Represent operators and literals as symbols. |
| `RecExpr<L>` | Store an expression as nodes whose children refer to earlier entries. |
| `parse_expr(...)` | Read an expression in the parenthesized text format. |
| `EGraph<L, A>` | Store nodes and their equivalence classes, with optional analysis. |
| `add_expr(expr)` | Add an expression and return its root e-class ID. |
| `merge(a, b)` | Declare two e-classes equivalent. |
| `rebuild()` | Restore graph consistency after additions or merges. |
| `find(id)` | Resolve an ID to its current canonical e-class. |

`SymbolLang` does not evaluate arithmetic. Rewrite rules or application
analysis supply those facts. See [Expressions and matching](02_expression_and_matching.md).

## Matching and rewriting

| API | Purpose |
| --- | --- |
| `parse_pattern(...)` | Read a pattern with variables such as `?x`. |
| `match(graph, pattern, root)` | Search a root class and return variable bindings. |
| `rewrite(name, lhs, rhs)` | Construct a named text-based rewrite. |
| `Condition<L, A>` | Decide whether a particular match may be rewritten. |
| `MultiPattern<L>` | Join several pattern clauses through shared variables. |

Search a clean graph: call `rebuild()` after manual graph changes. The runner
handles rebuilding during optimization. See
[Conditional rewrites](03_conditional_rewrites.md).

## Running and extraction

| API | Purpose |
| --- | --- |
| `run(graph, rules)` | Run rules until saturation or a limit. |
| `RunOptions` | Set iteration, node, match, time, and memory limits. |
| `RunReport` | Inspect the stop reason and iteration statistics. |
| `CompiledRules<L, A>` | Share immutable compiled rules between runs. |
| `Runner<L, A>` | Keep execution state across `resume()` calls. |
| `Extractor<L, A>` | Choose an expression using a tree cost policy. |
| `DagExtractor<L, A>` | Search for an expression that accounts for shared subexpressions. |

Check the report's stop reason before assuming saturation. The default tree
cost counts AST nodes. Recreate an extractor after changing its graph. See
[Reusable runners](reusable_runners.md) and [Advanced features](advanced_features.md).

## Inspection

Use `to_string(...)` to print supported values and `to_dot(graph)` to export
Graphviz data. `last_rebuild_stats()`, `memory_stats()`, and runner history
help diagnose graph growth and execution work. See
[Performance and diagnostics](performance.md).

The
[public headers](https://github.com/5yearsKim/c-egg/tree/main/include/eggc)
contain declarations and contract comments. [Code structure](architecture.md)
explains how those headers relate to their implementations.
