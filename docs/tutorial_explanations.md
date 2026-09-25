# Equivalences and unsafe rewrites

This adapts Rust `egg`'s [explanations tutorial](https://egraphs-good.github.io/egg/egg/tutorials/_03_explanations/). The complete runnable program is [`examples/tutorial_explanations.cpp`](../examples/tutorial_explanations.cpp).

Examples are disabled by default. From the repository root, enable them, then build and run this tutorial with CMake and a C++17 compiler:

```sh
cmake -S . -B build -DEGGC_BUILD_EXAMPLES=ON
cmake --build build --target tutorial_explanations
./build/examples/tutorial_explanations
```

The example uses the original five rewrites, including division rules that are unsafe when the denominator is zero:

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

Running them on the tutorial's fraction makes its e-class equivalent to `1`. Starting with `0` alone also puts `1` and `(* (/ 1 0) 0)` in the same class:

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

The program prints:

```text
(/ (* (/ 2 3) (/ 3 2)) 1) = 1: yes
0 = 1 with unsafe rules: yes
witness: 0 <- (* (/ 1 0) 0) -> 1
rules: times-zero, cancel-denominator
```

`times-zero` equates the witness with `0`; `cancel-denominator` equates it with `1`, even though its denominator is zero. `egg-c` checks these equivalences and the witness, but it does not currently generate Rust `egg`'s `FlatExplanation` or `TreeExplanation`.
