# Getting started

An e-graph stores expressions that are known to be equivalent. Rewrite rules
add equivalent expressions; extraction chooses a result with the lowest cost.
This example follows [egg's introductory tutorial](https://docs.rs/egg/latest/egg/tutorials/_02_getting_started/index.html).

## Your first optimizer

Save this as `main.cpp`:

```cpp
#include <eggc/all.hpp>
#include <iostream>
#include <vector>

int main() {
    using eggc::rewrite;
    auto input = eggc::parse_expr("(+ 0 (* 1 a))");
    std::vector rules{
        rewrite("commute-add", "(+ ?x ?y)", "(+ ?y ?x)"),
        rewrite("commute-mul", "(* ?x ?y)", "(* ?y ?x)"),
        rewrite("add-zero", "(+ ?x 0)", "?x"),
        rewrite("mul-zero", "(* ?x 0)", "0"),
        rewrite("mul-one", "(* ?x 1)", "?x"),
    };

    eggc::EGraph<eggc::SymbolLang> graph;
    auto root = graph.add_expr(input);
    auto report = eggc::run(graph, rules);
    if (report.reason != eggc::StopReason::Saturated) {
        std::cerr << "Optimization reached a limit\n";
        return 1;
    }

    auto [cost, best] = eggc::Extractor<eggc::SymbolLang>(graph).find_best(root);
    std::cout << eggc::to_string(best) << " (cost " << cost << ")\n";
}
```

From the repository root:

```sh
c++ -std=c++20 -Iinclude main.cpp -o /tmp/first-eggc
/tmp/first-eggc
```

The result is `a (cost 1)`. Operators appear first in parentheses: `(+ a b)`
means `a + b`. Pattern variables such as `?x` match any e-class. `run` repeatedly
applies the rules; `Extractor` chooses the smallest AST in the original root's
class. `SymbolLang` treats numbers as symbols; there is no automatic constant folding.

## Inspect the saturated graph

After `run`, print every equivalence class and its stored nodes:

```cpp
std::cout << "Root: e" << graph.find(root) << '\n';
std::cout << eggc::to_string(graph);
```

Each `eN:` heading identifies an e-class; its indented nodes are equivalent.
A node such as `(+ e0 e4)` refers to child classes, so cycles appear as class
references instead of being expanded. All classes are printed, including those
not reachable from the root. The dump ends with a newline; an empty graph prints
an empty string. Nodes retain their stored order within each class.

`eggc::to_string(node)` prints a single node with its stored child IDs.
For custom languages, both printers use `LanguageIO<L>::format_op(node)`.
Printing a graph requires a clean graph: call `rebuild()` after manual edits.
Check `report.reason == eggc::StopReason::Saturated` to distinguish saturation
from hitting a runner limit. Extraction still chooses one best expression from
the root class.

## Search a pattern

```cpp
eggc::EGraph<eggc::SymbolLang> graph;
auto root = graph.add_expr(eggc::parse_expr("(foo a b)"));
graph.rebuild();
auto pattern = eggc::parse_pattern("(foo ?x ?x)");
auto matches = eggc::match(graph, pattern, root); // Empty: a and b differ.
```

Repeated variables must match the same e-class. After merging `a` and `b`, the
pattern matches. Call `rebuild()` after manual additions or merges before
matching or extraction. `run()` handles rebuilding for you. Recreate an extractor
if you mutate its graph.

Use quoted symbols for spaces or literal names beginning with `?`, for example
`(foo "two words" "?literal")`. Parsing errors throw `eggc::ParseError` with
`offset()`, `line()`, and `column()`; invalid rules throw `std::invalid_argument`.

## Run the complete example

The [example](../examples/tutorial_getting_started.cpp) checks matching before
and after a merge and verifies several simplifications:

```sh
bazel run //examples:tutorial_getting_started
bazel test //tests:all //examples:all
```

For CMake commands, see the [README](../README.md). Next, try
[custom languages](custom_languages.md) or [conditional rewrites](conditional_rewrites.md).
