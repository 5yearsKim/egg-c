# Conditional rewrites

In [Expressions and matching](02_expression_and_matching.md), a repeated pattern
variable required two operands to be equivalent. Sometimes that is not enough
to justify a rewrite: `x / x = 1` also requires `x` to be nonzero.

A **condition** is a function that returns `true` to allow a rewrite for a
particular match, or `false` to skip it.

| Input | Does `(/ ?x ?x)` match? | Is `?x` known to be nonzero? | Result |
|---|---|---|---|
| `(/ 2 2)` | Yes | Yes | `1` |
| `(/ 0 0)` | Yes | No | Unchanged |
| `(/ a a)` | Yes | Unknown | Unchanged |

All three inputs match the pattern. The condition decides which match may
produce a replacement. Not knowing the value of `a` means we cannot assume it
is nonzero.

The [runnable example](../examples/conditional_rewrite.cpp) checks integer
literals directly, using the ordinary `SymbolLang` graph. It needs no custom
analysis.

## Set up the example

Save the following three C++ code blocks, in order, in `main.cpp`.

```cpp
#include <eggc/all.hpp>
#include <charconv>
#include <iostream>
#include <vector>

using Node = eggc::SymbolLang;
using Graph = eggc::EGraph<Node>;
```

## Check the matched operand

Define a function that looks for a nonzero integer in the class matched by
`?x`. `SymbolLang` stores numbers as text, so `std::from_chars` reads that text
as an integer.

```cpp
bool is_nonzero(const Graph& graph, eggc::Id /* matched_class */,
                const eggc::Substitution& bindings) {
  for (const auto& node : graph.nodes(bindings.at("?x"))) {
    if (!node.args.empty()) continue;  // Skip expressions such as (+ 1 1).

    int value;
    auto [end, error] =
        std::from_chars(node.op.data(), node.op.data() + node.op.size(), value);
    if (error != std::errc{} || end != node.op.data() + node.op.size()) {
      continue;  // Symbols such as a are not known integers.
    }
    if (value != 0) return true;
  }
  return false;
}
```

`bindings.at("?x")` identifies the class matched by `?x`. `graph.nodes(...)`
returns its equivalent nodes. The function looks for an integer literal in
that class:

- `2` parses as a nonzero integer, so the function returns `true`.
- `0` parses, but fails `value != 0`.
- `a` does not parse as an integer, so it is skipped.

The parser reports an error for invalid or out-of-range integers. Checking
`end` also rejects partially parsed text, such as `2abc`. If no nonzero integer
is found, the function returns `false`. Operator nodes such as `(+ 1 1)` are
skipped too; this example does not evaluate arithmetic.

The callback's three parameters have different roles:

| Parameter | Meaning when matching `(/ 2 2)` |
|---|---|
| `graph` | Read-only access to the graph, used to inspect the operand's class. |
| `matched_class` | The class containing the whole division `(/ 2 2)`; unused here. |
| `bindings` | The variables for this match: `?x` maps to the class containing `2`. |

`bindings` is an `eggc::Substitution`, a map from pattern variable names to
class IDs. Each match has its own bindings. The loop inspects nodes in that one
operand class, which may contain several equivalent expressions; it does not
search every occurrence of `?x` in the graph.

The callback signature requires the whole match's class ID even when the
function does not use it. `/* matched_class */` is a comment documenting that
unnamed parameter.

## Attach the condition and run the rule

```cpp
int main() {
    eggc::Condition<Node> nonzero{"nonzero", {"?x"}, is_nonzero};
    auto rule = eggc::rewrite("div-self", "(/ ?x ?x)", "1", nonzero);

    for (const auto* input : {"(/ 2 2)", "(/ 0 0)", "(/ a a)"}) {
        Graph graph;
        auto root = graph.add_expr(eggc::parse_expr(input));
        auto report = eggc::run(graph, std::vector{rule});
        if (report.reason != eggc::StopReason::Saturated) {
            std::cerr << "Optimization reached a limit\n";
            return 1;
        }
        auto [cost, best] = eggc::Extractor<Node>(graph).find_best(root);
        std::cout << input << " -> " << eggc::to_string(best) << '\n';
    }
}
```

The condition's initializer supplies three things:

- `"nonzero"`: a name for the condition.
- `{"?x"}`: the pattern variables the function reads.
- `is_nonzero`: the function to call for each match.

The fourth argument to `rewrite` attaches this condition. For `(/ 2 2)`, the
function returns `true`, so the runner adds `1` to the division's equivalence
class. Extraction chooses `1`. The operand `2` is unchanged.

For `(/ 0 0)` and `(/ a a)`, the function returns `false`, so the runner skips
the replacement. As in the first tutorial, `run()` handles rebuilding the graph.

## Run the example

Compile your `main.cpp` from the repository root:

```sh
c++ -std=c++20 -Iinclude main.cpp -o /tmp/eggc-conditional
/tmp/eggc-conditional
```

Or run the repository's example, which also verifies the results:

```sh
bazel run //examples:conditional_rewrite
```

```text
(/ 2 2) -> 1
(/ 0 0) -> (/ 0 0)
(/ a a) -> (/ a a)
```

Other representable nonzero integer literals, such as `3` and `-2`, also satisfy
the condition. Merely failing to find a zero node does not prove nonzero: the
function must find a known nonzero integer. Other conditions should likewise
rely on facts that remain valid as equivalent expressions are added.

Next, try [custom languages](04_custom_langage.md) to represent operators and
values with your own node type.
