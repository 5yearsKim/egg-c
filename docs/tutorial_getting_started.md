# Getting started with `egg-c`

This is the C++ version of Rust `egg`'s [getting-started tutorial](https://egraphs-good.github.io/egg/egg/tutorials/_02_getting_started/). The complete runnable program is [`examples/tutorial_getting_started.cpp`](../examples/tutorial_getting_started.cpp).

From the repository root, build and run it with CMake and a C++17 compiler:

```sh
cmake -S . -B build -DEGGC_BUILD_EXAMPLES=ON
cmake --build build --target tutorial_getting_started
./build/examples/tutorial_getting_started
```

First, parse an expression and add the same structure to an e-graph. Its two IDs refer to the same e-class after rebuilding:

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

A repeated pattern variable must match the same e-class twice. `(foo ?x ?x)` does not match until `a` and `b` are merged and the graph is rebuilt:

```cpp
const eggc::Pattern repeated = eggc::parse_pattern("(foo ?x ?x)");
const bool before = !eggc::match(graph, repeated, foo).empty(); // false
graph.merge(a, b);
graph.rebuild();
const bool after = !eggc::match(graph, repeated, foo).empty();  // true
```

Finally, run the tutorial's arithmetic rewrites and extract the smallest expression from the input's e-class:

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
eggc::run(optimizer, rules);
const auto [cost, best] = eggc::Extractor(optimizer).find_best_rec_expr(root);
// eggc::to_string(best) == "a" and cost == 1
```

The program prints:

```text
expression: (foo a b)
same expression shares a class: yes
(foo ?x ?x) before merge: no; after merge: yes
best: a (AST size 1)
```
