# Custom languages

The previous tutorials used `SymbolLang`, which stores operators and literals
as symbols. A custom language lets your application choose how to represent
those values. Here we use `MyFilterNode` to represent a product search filter
with boolean literals, named fields, and logical operators.

An app combining optional search settings might build this filter:

```text
true AND (in_stock OR false)
```

It can simplify to `in_stock` while selecting the same products. We will encode
that filter as `(and true (or in_stock false))` and simplify it with three rules.
The [complete example](../examples/custom_language.cpp) is runnable with Bazel.

## Define the data and node operations

Save the following three C++ code blocks, in order, in `main.cpp`.

The first block defines `MyFilterNode` and the operations egg-c needs to store
and match it:

```cpp
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "eggc/all.hpp"

namespace example {
// A search filter has boolean literals, named fields, and logical operators.
struct MyFilterNode {
  enum class Kind { Boolean, Field, And, Or, Not };
  using Discriminant = Kind;
  Kind kind;
  bool value = false;
  std::string field;
  std::vector<eggc::Id> args;
  Kind discriminant() const { return kind; }
  const auto& children() const { return args; }
  auto& children_mut() { return args; }
  bool matches(const MyFilterNode& other) const {
    return kind == other.kind && value == other.value && field == other.field &&
           args.size() == other.args.size();
  }
  bool operator==(const MyFilterNode&) const = default;
  std::size_t hash() const {
    std::size_t seed = std::hash<bool>{}(value);
    eggc::hash_combine(seed, std::hash<std::string>{}(field));
    eggc::hash_combine(seed, static_cast<std::size_t>(kind));
    for (auto child : args) eggc::hash_combine(seed, child);
    return seed;
  }
};
static_assert(eggc::Language<MyFilterNode>);
}  // namespace example
```

Each node represents one literal, field, or operator:

| Filter part | `kind` | Stored data | Children |
|---|---|---|---|
| `true` | `Boolean` | `value = true` | None |
| `false` | `Boolean` | `value = false` | None |
| `in_stock` | `Field` | `field = "in_stock"` | None |
| `(and true in_stock)` | `And` | No extra data | Two IDs |
| `(or in_stock false)` | `Or` | No extra data | Two IDs |
| `(not in_stock)` | `Not` | No extra data | One ID |

A node's `args` stores child IDs, not nested C++ objects. In a parsed expression,
those IDs refer to earlier nodes in the expression. Once stored in an e-graph,
they refer to equivalence classes.

`value` and `field` are unused for operator nodes. The parser consistently sets
those unused fields to `false` and an empty string so that equal operators have
the same representation.

### Why these methods are needed

| Member | What egg-c uses it for |
|---|---|
| `Discriminant` and `discriminant()` | Group candidate nodes by their kind during search. |
| `children()` | Read the node's operands. |
| `children_mut()` | Update operand IDs as the graph merges classes. |
| `matches(other)` | Compare kind, attributes, and operand count during pattern matching. |
| `operator==` | Check complete node identity, including child IDs. |
| `hash()` | Find identical nodes efficiently; equal nodes must have equal hashes. |

`matches` leaves child IDs out because the matcher checks the operands
separately. For example, two `And` nodes with different children have the same
operator shape, but they are not identical nodes. Field names and boolean
values still matter: `in_stock` must not match `on_sale`, and `true` must not
match `false`.

`static_assert(eggc::Language<MyFilterNode>)` checks at compile time that the
required interface is present. It does not prove that your equality, matching,
and hashing logic is correct.

## Translate between text and nodes

`LanguageIO` connects the text format to `MyFilterNode`. Its specialization
belongs in namespace `eggc`:

```cpp
// Parsing and printing are optional extensions to the node contract.
namespace eggc {
template <>
struct LanguageIO<example::MyFilterNode> {
  using Node = example::MyFilterNode;
  static Node from_op(std::string_view op, std::vector<Id> children) {
    if (op == "and" || op == "or" || op == "not") {
      const auto arity = op == "not" ? 1u : 2u;
      if (children.size() != arity)
        throw std::invalid_argument("wrong number of filter operands");
      const auto kind = op == "and" ? Node::Kind::And
                      : op == "or"  ? Node::Kind::Or
                                    : Node::Kind::Not;
      return {kind, false, {}, std::move(children)};
    }
    if (!children.empty()) throw std::invalid_argument("unknown filter operator");
    if (op == "true" || op == "false")
      return {Node::Kind::Boolean, op == "true", {}, {}};
    return {Node::Kind::Field, false, std::string(op), {}};
  }
  static std::string format_op(const Node& node) {
    switch (node.kind) {
      case Node::Kind::Boolean: return node.value ? "true" : "false";
      case Node::Kind::Field: return node.field;
      case Node::Kind::And: return "and";
      case Node::Kind::Or: return "or";
      case Node::Kind::Not: return "not";
    }
    throw std::invalid_argument("unknown filter kind");
  }
};
}  // namespace eggc
```

`from_op` receives one token and its child IDs. It converts `true` and `false`
into actual `bool` values, converts logical operators into enum values, and
stores other leaf tokens as field names.

The parser checks operand counts: `and` and `or` take two operands, while `not`
takes one. An unknown operator with children, such as `(xor true false)`, is
rejected. The names `true`, `false`, `and`, `or`, and `not` are reserved by this
example's text format.

`format_op` converts one node back to its token, such as `and` or `in_stock`.
The library adds parentheses and formats children when printing an expression.
Parsing and printing are optional extensions; this example implements them so
its inputs and rules can be written as text.

## Add rules and extract the result

The optimizer uses the same APIs as [Getting started](01_getting_started.md),
with `MyFilterNode` as the node type:

```cpp
int main() {
  try {
    using Node = example::MyFilterNode;
    // Optional UI settings can leave redundant true/false terms in a filter.
    auto input = eggc::parse_expr<Node>("(and true (or in_stock false))");
    std::vector rules{
        eggc::rewrite<Node>("and-true", "(and true ?x)", "?x"),
        eggc::rewrite<Node>("or-false", "(or ?x false)", "?x"),
        eggc::rewrite<Node>("double-not", "(not (not ?x))", "?x"),
    };
    eggc::EGraph<Node> graph;
    auto root = graph.add_expr(input);
    auto report = eggc::run(graph, rules);
    auto [cost, best] = eggc::Extractor<Node>(graph).find_best(root);
    if (report.reason != eggc::StopReason::Saturated || cost != 1 ||
        best.nodes.back().kind != Node::Kind::Field ||
        best.nodes.back().field != "in_stock")
      throw std::runtime_error("filter simplification failed");
    std::cout << eggc::to_string(input) << " -> " << eggc::to_string(best)
              << '\n';
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
```

The rules express familiar boolean identities:

| Rule | Meaning |
|---|---|
| `(and true ?x) -> ?x` | Requiring an always-true condition changes nothing. |
| `(or ?x false) -> ?x` | An always-false alternative changes nothing. |
| `(not (not ?x)) -> ?x` | Negating twice restores the original condition. |

The input simplifies through these equivalences:

```text
(and true (or in_stock false))
(and true in_stock)
in_stock
```

`run` adds equivalent expressions to the graph. `Extractor` selects the
smallest expression from the input's class: the field node `in_stock`, with
cost `1`. The example checks its `kind` and `field` directly, demonstrating
that the extracted result is still your application-specific node type.

`in_stock` represents a boolean field on a product. The optimizer does not need
to know whether a particular product is in stock: the rules hold for either
field value. The `double-not` rule is also available; try replacing the input
with `(not (not in_stock))` to exercise it with the same expected result.

## Run the example

From the repository root:

```sh
bazel run //examples:custom_language
```

Or compile the three code blocks saved in `main.cpp`:

```sh
c++ -std=c++20 -Iinclude main.cpp -o /tmp/eggc-filter
/tmp/eggc-filter
```

Expected output:

```text
(and true (or in_stock false)) -> in_stock
```

See the [custom language reference](custom_languages.md) for more details on
node contracts, text support, and optional analysis.
