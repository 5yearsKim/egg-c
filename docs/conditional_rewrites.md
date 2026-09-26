# Conditional rewrites

A rule can require a fact before it applies. For example, `x / x = 1` requires
proof that `x` is nonzero. The [runnable example](../examples/conditional_rewrite.cpp)
uses `SymbolLang` with an analysis whose `Data` is `std::optional<int>`:
`nullopt` means unknown, and a value is a proven integer.

Given that analysis as `Values`, define the condition and rule:

```cpp
using Node = eggc::SymbolLang;
using Graph = eggc::EGraph<Node, Values>;

eggc::Condition<Node, Values> nonzero{
    "nonzero",
    {"?x"},
    [](const Graph& graph, eggc::Id,
       const eggc::Substitution& subst) {
        const auto& value = graph.analysis_data(subst.at("?x"));
        return value && *value != 0;
    },
};

auto rule = eggc::rewrite("div-self", "(/ ?x ?x)", "1", nonzero);
```

The condition declares the variables it reads. `rewrite` verifies that the
left-hand pattern binds them and infers the language and analysis from the
condition. Use the same analysis type in your graph and extractor.

The predicate reads a clean graph during search. Returning `false` skips the
match; returning `true` queues its replacement. Conditions should establish
facts that remain valid as equivalences are added. An unknown value fails this
condition: failing to find a zero node does not prove nonzero.

Run it:

```sh
bazel run //examples:conditional_rewrite
```

```text
(/ 2 2) -> 1
(/ 0 0) -> (/ 0 0)
(/ a a) -> (/ a a)
```

The example analysis recognizes integer leaves and rejects conflicting known
values during merges. It does not fold arithmetic. Several requirements can
be combined inside one predicate. The run report records condition checks and
rejections in each iteration.
