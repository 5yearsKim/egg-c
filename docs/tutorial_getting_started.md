# Getting started with `egg-c`

[All guides](README.md) · Next: [Practical usage](basic_usage.md)

In this tutorial, you will simplify `0 + (1 * a)` to `a`, then look at how
`egg-c` represents expressions and recognizes patterns. You need basic C++
knowledge, but no previous experience with e-graphs.

This adapts Rust `egg`'s [getting-started tutorial](https://egraphs-good.github.io/egg/egg/tutorials/_02_getting_started/).
The complete runnable program is
[`examples/tutorial_getting_started.cpp`](../examples/tutorial_getting_started.cpp).
The snippets below walk through parts of that program; they are not separate
source files.

## 1. Build and run the example

You need CMake 3.16 or newer, a compiler that supports C++17, and a local copy of
this repository. Run these commands from the repository root, the directory
containing `CMakeLists.txt`:

```sh
cmake -S . -B build -DEGGC_BUILD_EXAMPLES=ON
cmake --build build --target tutorial_getting_started
./build/examples/tutorial_getting_started
```

The first command configures the project in `build/` and enables examples, which
are disabled by default. The second compiles this tutorial and the library. The
third runs the resulting executable.

With a multi-configuration generator such as Visual Studio, build with
`--config Debug` and look in `build/examples/Debug/` for the executable (with an
`.exe` suffix on Windows).

Expected output:

```text
expression: (foo a b)
same expression shares a class: yes
(foo ?x ?x) before merge: no; after merge: yes
best: a (AST size 1)
```

If CMake cannot find a compiler, install or select a C++ toolchain first. If it
cannot find the tutorial target, rerun the configuration command with
`-DEGGC_BUILD_EXAMPLES=ON`. More common issues are covered in the
[troubleshooting table](basic_usage.md#troubleshooting).

## 2. Read the expression syntax

`egg-c` uses **S-expressions**: put the operator first, followed by its arguments,
inside parentheses. A name or number on its own is an **atom**, or leaf.

| Familiar notation | `egg-c` text | Meaning |
| --- | --- | --- |
| `a` | `a` | A leaf named `a`. |
| `x + 0` | `(+ x 0)` | A `+` node with two children. |
| `0 + (1 * a)` | `(+ 0 (* 1 a))` | A `+` node whose second child is a `*` expression. |
| `foo(a, b)` | `(foo a b)` | An operator named `foo` with two children. |

The parser reads structure. It does not evaluate arithmetic or assign meaning
to an operator name. A plain `EGraph` knows nothing special about `+`, `*`, or
`foo`; you supply equalities through rules, explicit merges, or an analysis.

## 3. Simplify your first expression

Here is the optimizer section of the runnable example:

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

`parse_expr` turns text into a `RecExpr`, a representation of one expression.
`add_expr` adds it to the e-graph and returns the ID of the class containing the
whole expression. Keep this `root` so you can ask for a result later.

Each `parse_rewrite` takes a descriptive name, a left-hand pattern to find, and a
right-hand pattern to add. For example, `add-0` says: find a sum whose right child
is `0`, bind its left child to `?x`, and make that sum equivalent to `?x`.
Pattern variables begin with `?`; the bare atom `x` would match the literal name
`x` instead.

One way to understand the resulting equalities is:

```text
(+ 0 (* 1 a))
    = (+ (* 1 a) 0)   commute-add
    = (* 1 a)         add-0
    = (* a 1)         commute-mul
    = a               mul-1
```

This is a hand-written explanation of the rules, not a trace printed by the
runner. The runner searches for matches throughout the graph, adds alternatives,
and merges their classes. It keeps earlier alternatives available. Notice why
the commutativity rules help: the input has `0` and `1` on the left, while the
identity rules expect them on the right.

`run` repeats search, application, and rebuilding until no further change occurs
or a limit is reached. Reaching a state with no further change is called
**equality saturation**. The returned `report` tells you why the run stopped;
the [practical guide](basic_usage.md#control-the-run-and-read-its-report) shows how
to inspect it.

Finally, `Extractor` chooses the cheapest expression represented in the root's
class. Its default cost is **AST size**, the number of nodes in the expression
tree. Here the original has five nodes (`+`, `0`, `*`, `1`, `a`), while `a` has
one. A small cost means a small expression under this policy; it does not measure
execution time.

## 4. Understand what the graph stores

These terms describe the objects you just used:

| Term | What it represents |
| --- | --- |
| Expression (`RecExpr`) | One expression, such as `(+ x 0)`. Internally, children refer to earlier nodes, so subexpressions can be shared. |
| E-node (`ENode`) | An operator and child e-class IDs. For a sum, the children refer to classes containing its operands. |
| E-class | A collection of e-nodes that the graph considers equivalent. After `add-0`, one class can represent both `x` and `(+ x 0)`. |
| E-graph (`EGraph`) | The collection of e-classes and their relationships. |
| Class ID (`Id`) | A handle to a class. `find(id)` returns its current representative after merges. IDs belong to the graph that created them. |

You can add an expression all at once or build it from its children:

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

`graph.add("a")` creates a leaf. `graph.add("foo", {a, b})` creates a parent
whose children are those two classes. Adding the same structure again reuses its
class. Compare `graph.find(left)` and `graph.find(right)` to check equivalence;
saved IDs can differ even after their classes have been merged.

## 5. Merge classes and rebuild

Calling `graph.merge(a, b)` asserts that `a` and `b` are equal. It is your
responsibility to justify that assertion in the language you are modeling.
This tutorial makes it just to demonstrate matching.

After additions or merges, call `rebuild()` before matching, extracting, or
querying class contents. Rebuilding restores the graph's invariants and
propagates equalities to parents. For example, if the graph contains `(f a)` and
`(f b)` and you merge `a` with `b`, rebuilding also unifies those two parent
expressions. This property is called **congruence**: equal inputs to the same
operator give equal expressions.

The usual manual workflow is:

```text
add expressions / merge classes -> rebuild -> match or extract
```

You can batch several changes before rebuilding. `run` handles rebuilding at its
start and after applying each iteration's rewrites.

## 6. Match a repeated variable

Continuing with `graph`, `a`, `b`, and `foo` from the previous snippet:

```cpp
const eggc::Pattern repeated = eggc::parse_pattern("(foo ?x ?x)");
const bool before = !eggc::match(graph, repeated, foo).empty(); // false
graph.merge(a, b);
graph.rebuild();
const bool after = !eggc::match(graph, repeated, foo).empty();  // true
```

`match` returns a collection of substitutions. Each substitution maps variable
names such as `?x` to matching e-class IDs. An empty collection means that the
pattern did not match in the requested class.

Using `?x` twice requires both children to belong to the same class. Before the
merge, `a` and `b` are distinct; afterward, either can serve as the same binding.
Matching uses the equalities already in the graph, so two children need not have
the same printed spelling to satisfy a repeated variable.

## Try it yourself

Edit the runnable example and rebuild its target after each change. It contains
checks for the original results, so update those checks when you intentionally
change the expected output.

1. Replace the optimizer input with `(+ b 0)`. What should extraction return?
2. Remove both commutativity rules and restore `(+ 0 (* 1 a))`. Which identity
   rules can match now?
3. Replace `(foo ?x ?x)` with `(foo ?x ?y)`. Does it match before the merge?

<details>
<summary>Answers</summary>

1. `b`, with AST size 1. The `add-0` rule matches directly.
2. None of the identity rules match. Their constants are on the right, so the
   input stays `(+ 0 (* 1 a))`, with AST size 5.
3. Yes. Distinct pattern variables may bind to different classes. They may also
   bind to the same class; different names do not require different values.

</details>

Continue to [Practical usage](basic_usage.md) for a complete program with constant
folding, guarded rules, and run limits.
