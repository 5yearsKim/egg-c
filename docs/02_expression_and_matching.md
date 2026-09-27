# Expressions and matching

An optimizer can simplify a value minus itself to zero. But how can it recognize
that `(2 + 3) - 5` subtracts two equal values when the operands look different?
This example shows how recording an equality makes a pattern match.

## Expressions describe data; patterns describe a search

An **expression** is concrete input, such as `(- (+ 2 3) 5)`, which means
`(2 + 3) - 5`. `parse_expr` reads it, and `add_expr` stores it in the e-graph.

A **pattern** is a search template. In `(- ?value ?value)`, `?value` is a
placeholder. Using the same variable twice requires both operands to belong to
the same **equivalence class**: a group of expressions known to be equal.

| Expression | Matches `(- ?value ?value)`? |
|---|---|
| `(- 5 5)` | Yes: both operands are already the same. |
| `(- 4 5)` | No: the operands are different. |
| `(- (+ 2 3) 5)` | Only after the graph knows that `(+ 2 3)` equals `5`. |

`SymbolLang` stores operators and numbers as symbols. It does not evaluate
`2 + 3`, so we must supply that arithmetic fact ourselves.

## Match before and after recording an equality

Save this as `main.cpp`:

```cpp
#include <eggc/all.hpp>
#include <iostream>

int main() {
    auto expr = eggc::parse_expr("(- (+ 2 3) 5)");
    eggc::EGraph<eggc::SymbolLang> graph;
    auto root = graph.add_expr(expr);

    // Retrieve the classes of the two operands already stored in the graph.
    auto sum = graph.add_expr(eggc::parse_expr("(+ 2 3)"));
    auto five = graph.add_expr(eggc::parse_expr("5"));
    graph.rebuild();

    auto pattern = eggc::parse_pattern("(- ?value ?value)");
    auto before = eggc::match(graph, pattern, root);
    std::cout << "Matches before merge: " << before.size()
              << " (2 + 3 and 5 are not yet known to be equal)\n";

    // Tell the graph that 2 + 3 equals 5.
    graph.merge(sum, five);
    graph.rebuild();

    auto after = eggc::match(graph, pattern, root);
    std::cout << "Matches after merge: " << after.size()
              << " (both operands are now known to be equal)\n";
}
```

From the repository root:

```sh
c++ -std=c++20 -Iinclude main.cpp -o /tmp/eggc-matching
/tmp/eggc-matching
```

Output:

```text
Matches before merge: 0 (2 + 3 and 5 are not yet known to be equal)
Matches after merge: 1 (both operands are now known to be equal)
```

## What changed?

`match(graph, pattern, root)` searches the class containing our subtraction for
ways to satisfy the pattern. Each result binds pattern variables to equivalence
classes. Initially, `(+ 2 3)` and `5` are in separate classes, so they cannot both
bind to `?value` and there are no matches.

`merge(sum, five)` declares those two classes equivalent. It accepts an equality
provided by our code; it does not prove the equality or calculate the sum.
Adding an expression already present in the graph retrieves its class, which is
why the `sum` and `five` IDs refer to the operands of our original subtraction.

`rebuild()` restores the graph's consistency after manual additions or merges.
Call it before matching. In the [optimizer tutorial](01_getting_started.md),
`run()` handles rebuilding for you.

After the merge and rebuild, both operands belong to the same class. The pattern
now has one match, whose `?value` binding is that shared class. The runnable
example checks this using `after.front().at("?value") == graph.find(five)`.

## Why this helps an optimizer

The pattern is the left side of the subtraction rule
`(- ?value ?value) -> 0`. Recording the arithmetic fact makes that rule applicable
to `(2 + 3) - 5`, even though its operands were written differently.

This example only searches: `match` does not apply a rewrite or turn the result
into zero. In an optimizer, a constant-folding analysis or a rewrite could supply
the equality, and the subtraction rule could then use it.

## Run the complete example

The [runnable example](../examples/tutorial_expression_and_matching.cpp) verifies
both the absence of a match before merging and the variable binding afterward:

```sh
bazel run //examples:tutorial_expression_and_matching
bazel test //examples:tutorial_expression_and_matching_test
```

For CMake commands, see the [README](../README.md). Next, learn how to control
when a rule applies in [Conditional rewrites](03_conditional_rewrites.md).
