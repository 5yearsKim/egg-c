# Equivalences and unsafe rewrites

[All guides](README.md) · Previous: [Practical usage](basic_usage.md)

An e-graph trusts the equalities you give it. This tutorial shows how unsafe
division rules make it report `0 = 1`, then examines the expression that exposes
the mistake. It adapts Rust `egg`'s [explanations tutorial](https://egraphs-good.github.io/egg/egg/tutorials/_03_explanations/).
The complete program is
[`tutorial_explanations.cpp`](../examples/tutorial_explanations.cpp).

## Run the example

Using the same CMake and C++17 setup as [Getting started](tutorial_getting_started.md),
run from the repository root:

```sh
cmake -S . -B build -DEGGC_BUILD_EXAMPLES=ON
cmake --build build --target tutorial_explanations
./build/examples/tutorial_explanations
```

Expected output:

```text
(/ (* (/ 2 3) (/ 3 2)) 1) = 1: yes
0 = 1 with unsafe rules: yes
witness: 0 <- (* (/ 1 0) 0) -> 1
rules: times-zero, cancel-denominator
```

The first input represents `(2/3 * 3/2) / 1`, which equals `1` in rational
arithmetic. C++ integer division would give a different result. You choose the
operator semantics; the parser only stores their structure.

## The unsafe rules

The example uses a plain `EGraph` with these five rules and no constant analysis:

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

These rules omit necessary assumptions. In rational arithmetic, cancellation
requires a nonzero denominator, and inversion requires both operands to be
nonzero. Multiplication by zero also assumes its other operand has a defined
value. A pattern match alone does not establish those facts.

## Find the conflicting expression

After running the rules from `0`, query its class for `1` and a specific
**witness**: an expression that exposes the conflict.

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

The witness `(* (/ 1 0) 0)` means `(1 / 0) * 0`. Two rules act on it:

| Rule | Bindings | Result |
| --- | --- | --- |
| `times-zero` | `?a = (/ 1 0)` | `0` |
| `cancel-denominator` | `?a = 1`, `?b = 0` | `1` |

Both results become equal to the same expression, so the graph merges `0` and
`1`. The arithmetic error is division by zero: cancellation is invalid here,
and multiplying an undefined value by zero does not make it defined.

This witness explains the conflict, but does not trace every step that generated
it. `egg-c` checks equivalences and this hand-selected witness; it currently does
not generate Rust `egg`'s `FlatExplanation` or `TreeExplanation` proof traces.

## Interpret equality queries

For existing IDs, compare `graph.find(left) == graph.find(right)` after
rebuilding. Alternatively, `match` checks whether an expression is represented in
an existing class, as above.

A positive result means the graph considers the expressions equal under its
accumulated assertions. A negative result means equality has not been established;
missing rules or run limits may explain why. Neither result independently
validates the mathematics of your rules.

## Add the right preconditions

For integer division, this guarded rule is valid when the common operand is
nonzero:

```cpp
const auto div_self = eggc::parse_rewrite(
    "div-self", "(/ ?x ?x)", "1", eggc::known_nonzero("?x"));
```

Use it with `ConstantAnalysis`, as in the
[conditional rewrite example](basic_usage.md#conditional-rewrites). It simplifies
`(/ 7 7)` to `1`, but rejects zero and unknown operands.

That guard does not repair all the original rules. Integer cancellation also
fails for nonzero denominators: `(3 / 2) * 2` evaluates to `2`. Rational rules
need facts about defined operands and nonzero denominators; `known_nonzero` only
recognizes exact nonzero integer facts.

When debugging a rule, reduce the input and rule set, inspect concrete variable
bindings, and try zero, negative, and unknown operands. After correcting a rule,
start with a fresh graph: removing a rule does not undo earlier merges.

**Try it:** apply the two witness rules to `(* (/ 8 0) 0)`. They would equate `0`
and `8` through the same invalid cancellation.
