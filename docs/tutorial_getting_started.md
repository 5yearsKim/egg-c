# Getting started with `egg-c`

[All guides](README.md) · Next: [Practical usage](basic_usage.md)

This tutorial simplifies `0 + (1 * a)` to `a` and introduces e-classes and pattern
matching. It adapts Rust `egg`'s [getting-started tutorial](https://egraphs-good.github.io/egg/egg/tutorials/_02_getting_started/).
The snippets come from the complete
[`tutorial_getting_started.cpp`](../examples/tutorial_getting_started.cpp) example.

## Build and run

With CMake 3.16 or newer and a C++17 compiler, run from the repository root:

```sh
cmake -S . -B build -DEGGC_BUILD_EXAMPLES=ON
cmake --build build --target tutorial_getting_started
./build/examples/tutorial_getting_started
```

The commands enable examples, build the tutorial, and run it. Expected output:

```text
expression: (foo a b)
same expression shares a class: yes
(foo ?x ?x) before merge: no; after merge: yes
best: a (AST size 1)
```

For Visual Studio or another multi-configuration generator, build with
`--config Debug` and use `build/examples/Debug/` (with `.exe` on Windows).

## Expressions and e-classes

Expressions use **S-expressions**: the operator comes first, followed by its
arguments. For example, `x + 0` becomes `(+ x 0)`, and `0 + (1 * a)` becomes
`(+ 0 (* 1 a))`. Names and numbers such as `a` and `0` are leaves.

The parser reads structure; rules and analyses supply the meaning of operators.
Here are the main objects you will use:

| Object | Meaning |
| --- | --- |
| `RecExpr` | One expression, with nodes that can share subexpressions. |
| E-node (`ENode`) | An operator whose children refer to e-classes. |
| E-class | A group of equivalent expressions, such as `x` and `(+ x 0)`. |
| `EGraph` | The collection of e-classes and their relationships. |
| `Id` | A class handle belonging to one graph; `find(id)` gives its current representative. |

## Simplify an expression

```cpp
eggc::EGraph optimizer;
const eggc::Id root = optimizer.add_expr(eggc::parse_expr("(+ 0 (* 1 a))"));
const std::vector<eggc::Rewrite> rules{
    eggc::parse_rewrite("commute-add", "(+ ?x ?y)", "(+ ?y ?x)"),
    eggc::parse_rewrite("commute-mul", "(* ?x ?y)", "(* ?y ?x)"),
    eggc::parse_rewrite("add-0", "(+ ?x 0)", "?x"),
    eggc::parse_rewrite("mul-0", "(* ?x 0)", "0"),
    eggc::parse_rewrite("mul-1", "(* ?x 1)", "?x"),
};
const auto report = eggc::run(optimizer, rules);
const auto [cost, best] = eggc::Extractor(optimizer).find_best_rec_expr(root);
// eggc::to_string(best) == "a" and cost == 1
```

`parse_expr` reads the input, and `add_expr` returns its root class ID. Each
`parse_rewrite` takes a name, a pattern to find, and a replacement pattern.
Variables start with `?`: `(+ ?x 0)` matches any left operand followed by zero,
while `(+ x 0)` specifically matches the literal name `x`.

The rules justify this sequence:

```text
(+ 0 (* 1 a))
    = (+ (* 1 a) 0)   commute-add
    = (* 1 a)         add-0
    = (* a 1)         commute-mul
    = a               mul-1
```

The commutativity rules move `0` and `1` to the right, where the identity rules
expect them. The graph retains equivalent alternatives as it discovers them.

`run` repeats matching, rule application, and rebuilding until no further change
occurs (**saturation**) or a limit is reached. Its report records the stop reason.
`Extractor` then selects the cheapest expression represented in the root class.
The default cost is **AST size**, the number of expression-tree nodes: the input
costs 5, while `a` costs 1.

## Add, merge, and rebuild

You can build expressions from children or add a parsed expression directly:

```cpp
const eggc::RecExpr expr = eggc::parse_expr("(foo a b)");
eggc::EGraph graph;
const eggc::Id a = graph.add("a");
const eggc::Id b = graph.add("b");
const eggc::Id foo = graph.add("foo", {a, b});
const eggc::Id foo_again = graph.add_expr(expr);
graph.rebuild();
// graph.find(foo) == graph.find(foo_again)
```

Both ways produce the same class. To check equivalence after merges, compare
`graph.find(left)` and `graph.find(right)`; the saved IDs themselves may differ.

`merge(a, b)` asserts equality. After manual additions or merges, call `rebuild()`
before matching, extracting, or querying class contents. Rebuilding propagates
equalities to parents: merging `a` and `b` also makes `(f a)` and `(f b)` equal.
This property is called **congruence**. The runner handles rebuilding for you.

## Match a repeated variable

Continuing with the graph above:

```cpp
const eggc::Pattern repeated = eggc::parse_pattern("(foo ?x ?x)");
const bool before = !eggc::match(graph, repeated, foo).empty(); // false
graph.merge(a, b);
graph.rebuild();
const bool after = !eggc::match(graph, repeated, foo).empty();  // true
```

Using `?x` twice requires both children to belong to the same e-class. The
explicit merge asserts `a = b` for this demonstration. In your own program,
only merge expressions you can justify as equal.

`match` returns substitutions from variable names to class IDs; an empty result
means no match. Different variables, as in `(foo ?x ?y)`, may bind to either
different classes or the same class.

**Try it:** change the optimizer input to `(+ b 0)`. It should extract `b` with
cost 1. If you edit the runnable example, update its expected-result checks too.

Continue to [Practical usage](basic_usage.md) for a complete program with constant
folding and conditional rules.
