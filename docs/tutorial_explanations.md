# Equivalences and unsafe rewrites

[All guides](README.md) · Previous: [Practical usage](basic_usage.md)

An e-graph trusts the equalities you give it. In this tutorial, you will see how
an unsafe division rule makes the graph report `0 = 1`, inspect an expression
that exposes the problem, and work out which assumptions a rule needs.

Read [Getting started](tutorial_getting_started.md) first for e-classes and
matching, and [Practical usage](basic_usage.md#conditional-rewrites) for conditions.
This adapts Rust `egg`'s [explanations tutorial](https://egraphs-good.github.io/egg/egg/tutorials/_03_explanations/).
The complete runnable program is
[`examples/tutorial_explanations.cpp`](../examples/tutorial_explanations.cpp).

## Run the example

With CMake 3.16 or newer and a C++17 compiler, run these commands from the
repository root. Examples are disabled by default, so enable them first:

```sh
cmake -S . -B build -DEGGC_BUILD_EXAMPLES=ON
cmake --build build --target tutorial_explanations
./build/examples/tutorial_explanations
```

With a multi-configuration generator, add `--config Debug` to the build command
and run the executable in `build/examples/Debug/` (with `.exe` on Windows).

Expected output:

```text
(/ (* (/ 2 3) (/ 3 2)) 1) = 1: yes
0 = 1 with unsafe rules: yes
witness: 0 <- (* (/ 1 0) 0) -> 1
rules: times-zero, cancel-denominator
```

The second line is the deliberate failure this tutorial investigates. It means
the supplied rules caused the graph to put `0` and `1` in the same class. It does
not establish a valid arithmetic identity.

## Understand the intended arithmetic

The first input means `(2/3 * 3/2) / 1`. Under rational arithmetic it equals `1`.
It does not equal `1` under C++ integer division, where `2 / 3` truncates to zero.
Before writing rules, decide what your expressions mean: the parser stores `/`
as an operator name and does not choose a division semantics for you.

The example uses a plain `EGraph`, with no `ConstantAnalysis`. All of its
arithmetic equalities come from these five rewrites:

```cpp
const std::vector<eggc::Rewrite> rules{
    eggc::parse_rewrite("div-one", "?x", "(/ ?x 1)"),
    eggc::parse_rewrite("unsafe-invert-division", "(/ ?a ?b)",
                        "(/ 1 (/ ?b ?a))"),
    eggc::parse_rewrite("simplify-frac", "(/ ?a (/ ?b ?c))",
                        "(/ (* ?a ?c) (* (/ ?b ?c) ?c))"),
    eggc::parse_rewrite("cancel-denominator", "(* (/ ?a ?b) ?b)", "?a"),
    eggc::parse_rewrite("times-zero", "(* ?a 0)", "0"),
};
```

Here is how to read them, assuming rational arithmetic with division defined
only for nonzero denominators:

| Rule | Informal equality | Assumptions to examine |
| --- | --- | --- |
| `div-one` | `x = x / 1` | Defined `x`; the direction expands expressions. |
| `unsafe-invert-division` | `a / b = 1 / (b / a)` | Both `a` and `b` must be nonzero. |
| `simplify-frac` | `a / (b / c) = (a * c) / ((b / c) * c)` | Both `b` and `c` must be nonzero. |
| `cancel-denominator` | `(a / b) * b = a` | `b` must be nonzero. |
| `times-zero` | `a * 0 = 0` | `a` must denote a defined value. |

These rules have no guards in this example. In particular, matching `?a` does
not prove that its expression has a defined numeric value. Also notice that
`div-one` searches every class because its left side is just `?x`; rules that
expand expressions can consume the runner's budget. Finding the equalities
printed here does not require the run to reach saturation.

## Ask whether the graph knows an equality

The runnable example uses this helper:

```cpp
bool equivalent(eggc::EGraph& graph, const char* lhs, const char* rhs) {
    const auto left = graph.add_expr(eggc::parse_expr(lhs));
    const auto right = graph.add_expr(eggc::parse_expr(rhs));
    graph.rebuild();
    return graph.find(left) == graph.find(right);
}
```

It adds both expressions, rebuilds, and compares their current class
representatives. This helper changes the graph by adding expressions. It does
not run rewrites; it checks equality already recorded or implied by congruence
and any attached analysis after rebuilding.

A `true` answer means that the graph considers the expressions equal under its
accumulated assertions. A `false` answer means that it has not established their
equality. Missing rules or run limits can leave mathematically equal expressions
in separate classes.

To query an existing class without adding expressions, use `match`. After
running the rules starting from `0`, the example looks for both `1` and a
particular problematic expression in the original class:

```cpp
eggc::EGraph zero;
const eggc::Id zero_root = zero.add_expr(eggc::parse_expr("0"));
eggc::run(zero, rules);
const bool zero_equals_one =
    !eggc::match(zero, eggc::parse_pattern("1"), zero_root).empty();
const bool witness_exists =
    !eggc::match(zero, eggc::parse_pattern("(* (/ 1 0) 0)"), zero_root).empty();
// Both values are true.
```

These patterns contain no variables. Their matches simply establish that those
exact expression structures are represented in the requested class.

## Follow the division-by-zero witness

A **witness** here is a concrete expression that makes the incompatible uses of
the rules visible. Read the printed expression as `(1 / 0) * 0`:

```text
                    (* (/ 1 0) 0)
                       /       \
             times-zero       cancel-denominator
                   /             \
                  0               1
```

For `times-zero`, bind `?a` to `(/ 1 0)`. Its pattern `(* ?a 0)` then matches the
witness and equates it with `0`.

For `cancel-denominator`, bind `?a` to `1` and `?b` to `0`. Its pattern
`(* (/ ?a ?b) ?b)` matches the same witness and equates it with `1`.

Equality is transitive, so the graph merges `0` and `1` through this common
expression. The graph is carrying out the supplied rules; the arithmetic problem
is that `1 / 0` has no defined rational value. Cancellation needs a nonzero
denominator, and multiplication by zero cannot make an undefined operand valid.

The witness explains the last conflicting equalities. It is not a complete
record of how the runner generated that expression from the starting `0`.
`egg-c` currently checks these equivalences and this hand-selected witness; it
does not generate Rust `egg`'s `FlatExplanation` or `TreeExplanation` proof traces.

## Make the rule's assumptions explicit

For a new rule, first choose the number system and the expressions it allows.
For example, this rule from the practical guide is valid for integer division
when the common operand is nonzero:

```cpp
const auto div_self = eggc::parse_rewrite(
    "div-self", "(/ ?x ?x)", "1", eggc::known_nonzero("?x"));
```

Use it with a graph constructed with `ConstantAnalysis`, as shown in the
[complete guarded example](basic_usage.md#conditional-rewrites). It simplifies
`(/ 7 7)` but rejects both `(/ 0 0)` and `(/ x x)` when `x` is unknown. It
illustrates how to encode a precondition; it is a separate rule from the five
unsafe rules above.

A nonzero denominator alone would not repair `cancel-denominator` for integer
division: `(3 / 2) * 2` is `2`, not `3`. In a rational language, you would instead
need facts establishing that operands are defined and that denominators are
nonzero. `known_nonzero` only recognizes exact nonzero integer facts, so it does
not provide a general analysis of rational values or definedness. Repairing the
whole example requires reviewing all five rules under the chosen semantics.

When investigating a surprising equality:

1. Start with a small input and the rules involved in the suspected identity.
2. Check the run report so you know whether a missing equality could be due to
   a limit.
3. Inspect a concrete expression and the variable bindings that connect the two
   results, as with the witness above.
4. Try boundary cases such as zero, negative values, and unknown symbols.
5. Correct the rule or its conditions, then use a fresh graph. Removing a rule
   does not undo equalities already merged into an existing graph.

An attached constant analysis can detect conflicting known values and raise
`AnalysisConflict`, but that does not replace justifying your rewrites. Many
incorrect symbolic equalities do not produce a detectable constant conflict.

## Try it yourself

1. Write down the bindings for both rules on `(* (/ 8 0) 0)`. Which two literals
   would these rules equate if the graph contained that expression?
2. Why is `known_nonzero("?x")` allowed to reject `(/ x x)` even if you intend
   `x` to be positive?
3. If an equivalence query returns false after a run stops at its iteration
   limit, have you proved that the expressions are unequal?

<details>
<summary>Answers</summary>

1. `times-zero` binds `?a` to `(/ 8 0)` and yields `0`.
   `cancel-denominator` binds `?a` to `8` and `?b` to `0`, yielding `8`.
   The graph would equate `0` and `8`.
2. The graph needs evidence of the condition. A symbol's name does not encode
   your assumption, and the built-in condition requires an exact constant fact.
3. No. You only know that the current graph has not established equality.

</details>

Return to the [guide index](README.md), or keep the
[practical troubleshooting table](basic_usage.md#troubleshooting) handy while
writing your own examples.
