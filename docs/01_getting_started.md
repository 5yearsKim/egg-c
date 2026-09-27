# Getting started

An e-graph stores expressions that are known to be equivalent. Rewrite rules
add equivalent expressions; extraction chooses a result with the lowest cost.
This example follows [egg's introductory tutorial](https://docs.rs/egg/latest/egg/tutorials/_02_getting_started/index.html).

## Your first optimizer

Simplify `0 + (1 * a)` to `a` using familiar arithmetic identities.

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

## Run the complete example

The [example](../examples/tutorial_getting_started.cpp) runs the optimizer
and verifies several simplifications:

```sh
bazel run //examples:tutorial_getting_started
bazel test //tests:all //examples:all
```

For CMake commands, see the [README](../README.md). Next, learn how expressions,
patterns, and equivalence classes work in
[Expressions and matching](02_expression_and_matching.md).
