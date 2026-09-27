# Custom language reference

For a step-by-step introduction, see [Custom languages](04_custom_langage.md).

Start with `SymbolLang` for symbolic expressions. Define your own node when you
need typed literals, fixed operator sets, or semantic attributes. The complete
[custom-language example](../examples/custom_language.cpp) simplifies a search
filter built from optional UI settings:

```text
true AND (in_stock OR false) -> in_stock
```

Its `MyFilterNode` stores boolean literals as `bool`, field names such as
`in_stock` as strings, and operators as an enum:

```cpp
enum class Kind { Boolean, Field, And, Or, Not };
```

The filter describes a boolean condition on a product. Simplifying it preserves
which products match; the example does not fetch products or evaluate a field's
value. The rules remove `true AND x`, `x OR false`, and double negation.

## Define a node

A node must be copyable and provide these members:

| Member | Meaning |
| --- | --- |
| `Discriminant`, `discriminant()` | Hashable operator key used for indexing |
| `children() const`, `children_mut()` | Borrowed, sized, random-access ranges of child IDs |
| `matches(other)` | Compare operator, semantic attributes, and arity; ignore child IDs |
| `operator==` | Compare complete identity, including child IDs |
| `hash()` | Hash complete identity, consistently with equality |

In `MyFilterNode`, `matches` compares the kind, boolean value, field name, and
number of children. This keeps `true` distinct from `false`, and `in_stock`
distinct from other fields. Equality and hashing also include the child IDs.

Check the contract with `static_assert(eggc::Language<MyNode>)`. Child accessors
can return vector/array references or `std::span`. Keep semantic attributes
stable while a node is stored.

In `RecExpr<MyNode>`, child IDs index earlier nodes. In `EGraph<MyNode>`, they
refer to equivalence classes. Build children before parents and keep these ID
spaces separate.

## Add text support

Specialize `LanguageIO` in namespace `eggc`:

```cpp
namespace eggc {
template <> struct LanguageIO<MyNode> {
    static MyNode from_op(std::string_view token, std::vector<Id> children);
    static std::string format_op(const MyNode& node);
};
}
```

`from_op` translates a token into your node, preserving the supplied children
in order. Reject unknown operators, invalid arity, and malformed literals with
`std::invalid_argument`. `format_op` returns just the operator or literal text,
without parentheses or children. The parser/printer handles expression structure
and quoting. These two operations should round-trip node identity.

You can then write:

```cpp
using Node = example::MyFilterNode;
auto input = eggc::parse_expr<Node>("(and true (or in_stock false))");
std::vector rules{
    eggc::rewrite<Node>("and-true", "(and true ?x)", "?x"),
    eggc::rewrite<Node>("or-false", "(or ?x false)", "?x"),
    eggc::rewrite<Node>("double-not", "(not (not ?x))", "?x"),
};
eggc::EGraph<Node> graph;
```

The example's text format reserves `true`, `false`, `and`, `or`, and `not`.
Other leaf tokens are field names. `and` and `or` require two operands; `not`
requires one. Unknown operators and incorrect operand counts are rejected.

Parsing and printing are optional: nodes can use either capability independently
or be constructed entirely in C++. Existing `Pattern::node`, `Pattern::var`,
and custom rewrite callbacks remain available for rules with computed attributes.
The engine does not require a string representation.

The same `format_op` implementation supports `eggc::to_string(node)`,
`eggc::to_string(graph)`, and expression/pattern printing. Node printing shows
stored child IDs as `eN`; graph printing groups nodes by canonical e-class and
shows canonical child IDs. Include `eggc/printer.hpp` to use printing directly.
The graph must be rebuilt before printing; `eggc::run` handles this for you.

For analysis, supply a copyable `Data` type, `make(graph, node)`, and
`merge(into, from)`. Facts describe every representative of an e-class; merging
must be associative, commutative, and idempotent. Return `AnalysisMerge::Conflict`
to reject incompatible facts. Conditions can inspect those facts or check nodes
directly, as shown in [Conditional rewrites](03_conditional_rewrites.md).

`Data` may be any copyable type, including `bool`. `make()` must depend on the
node and its operand facts, be deterministic, and be monotone as facts grow.
Use a convergent domain, such as a finite set of facts. Rebuild propagates
changes through a dependency queue; rebuilding an already-clean graph does
not reevaluate facts. Mutating external state is not an invalidation mechanism.
Rejecting a conflicting union does not roll back previous changes made by an
entire rewrite or rebuild.

Manually constructed patterns must have every entry reachable from their final
root. Multiple child positions may reference the same earlier entry; disconnected
variables or nodes are rejected before execution.

```sh
bazel run //examples:custom_language
```

Expected output:

```text
(and true (or in_stock false)) -> in_stock
```
