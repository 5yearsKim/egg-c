# Learn `egg-c`

`egg-c` is a C++17 library for exploring equivalent expressions and choosing a
useful one. For example, you can teach it that adding zero changes nothing, then
use that rule to simplify `(+ x 0)` to `x`.

You do not need previous experience with e-graphs. The tutorials assume you can
read a small C++ program and run commands in a terminal.

## Choose a starting point

| Guide | What you will learn |
| --- | --- |
| [1. Getting started](tutorial_getting_started.md) | Build an example, understand e-classes, match patterns, and simplify your first expression. |
| [2. Practical usage](basic_usage.md) | Write a complete program, fold constants, guard rewrites, control a run, and choose an extraction cost. |
| [3. Equivalences and unsafe rewrites](tutorial_explanations.md) | Follow an incorrect equality back to an unsafe rule and understand the limits of equivalence queries. |

Read them in that order on your first visit. Each guide includes expected results
and small exercises with answers. The practical guide also covers optional
[differential verification](basic_usage.md#differential-verification-against-rust-egg)
and [benchmarks](basic_usage.md#benchmarks) for contributors.

## The workflow you will use

```text
expression text  ->  parsed expression  ->  e-graph
                                             |
                                  apply rules and rebuild
                                             |
                                  extract a low-cost expression
```

An e-graph remembers alternatives that your rules say are equal. Extraction picks
one of those alternatives according to a cost, such as the number of nodes in its
expression tree. The correctness of the result depends on the equalities you give
the graph.

Ready to run some code? Start with [Getting started](tutorial_getting_started.md).
