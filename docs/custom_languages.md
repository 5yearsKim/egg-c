# Custom languages

Start with `SymbolLang` for symbolic expressions. Define your own node when you
need typed literals, fixed operator sets, or semantic attributes. The complete
[custom-language example](../examples/custom_language.cpp) simplifies `(+ 7 0)`
using a node that stores numbers as integers.

## Define a node

A node must be copyable and provide these members:

| Member | Meaning |
| --- | --- |
| `Discriminant`, `discriminant()` | Hashable operator key used for indexing |
| `children() const`, `children_mut()` | Borrowed, sized, random-access ranges of child IDs |
| `matches(other)` | Compare operator, semantic attributes, and arity; ignore child IDs |
| `operator==` | Compare complete identity, including child IDs |
| `hash()` | Hash complete identity, consistently with equality |

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
auto input = eggc::parse_expr<MyNode>("(+ 7 0)");
auto rule = eggc::rewrite<MyNode>("add-zero", "(+ ?x 0)", "?x");
eggc::EGraph<MyNode> graph;
```

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
to reject incompatible facts. See [conditional rewrites](conditional_rewrites.md)
for a complete analysis example.

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
