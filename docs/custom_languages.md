# Custom language contracts

A language is your application's node type. The generic engine stores and
matches these nodes without requiring a text representation. Follow the
[custom language tutorial](04_custom_langage.md) for a complete example.

## Node contract

`eggc::Language<L>` requires a copyable, equality-comparable node type with
these members:

| Member | Contract |
| --- | --- |
| `Discriminant` | A copyable, equality-comparable type supported by `std::hash`. |
| `discriminant()` | Returns a `Discriminant` by value. Matching nodes have equal discriminants. |
| `children()` | Returns a borrowed, sized, random-access range of `const eggc::Id&`. |
| `children_mut()` | Returns the corresponding mutable range of `eggc::Id&`. |
| `matches(other)` | Returns `bool`; compares operator shape, attributes, and arity without comparing child IDs. |
| `operator==` | Compares complete node identity, including child IDs. |
| `hash()` | Returns `std::size_t`; includes all fields used by equality. |

Child ranges must also be common ranges, support `.size()`, and allow indexed
access. A reference to a vector or a borrowed view such as `std::span` can
satisfy this contract. Returning an owning vector by value does not.

Use `static_assert(eggc::Language<MyNode>)` to check the interface. The concept
cannot prove the behavior of your equality, hashing, or matching functions.
Equal nodes must have equal hashes. Keep a stored node's identity stable;
let the graph update child IDs through `children_mut()`.

See [`language.hpp`](https://github.com/5yearsKim/c-egg/blob/main/include/eggc/language.hpp)
for the exact constraints.

## Optional text support

Specialize `eggc::LanguageIO<MyNode>` to use the text parser and printer:

| Function | Responsibility |
| --- | --- |
| `static MyNode from_op(std::string_view, std::vector<eggc::Id>)` | Convert an operator or literal token and its children into a node. |
| `static std::string format_op(const MyNode&)` | Return the operator or literal token alone. |

`from_op` preserves child order and throws `std::invalid_argument` for an
unsupported token or operand count. The library handles parentheses and
pattern variables. `format_op` does not add parentheses or print children.

`eggc::ParseableLanguage<L>` and `eggc::PrintableLanguage<L>` check the two
extensions independently. You can use the engine with neither extension.

## Optional e-class analysis

The default `eggc::NoAnalysis<L>` stores no facts. To attach facts to each
equivalence class, provide an analysis type `A` to `eggc::EGraph<L, A>`:

| Member | Responsibility |
| --- | --- |
| `Data` | A copyable type holding facts about an entire e-class. |
| `make(const EGraph<L, A>&, const L&)` | Compute a node's facts from the node and its children's facts. |
| `merge(Data& into, const Data& from)` | Combine facts and return an `eggc::AnalysisMerge`. |

`A` must be move-constructible. Its merge operation must be associative,
commutative, and idempotent. `make` must be deterministic and monotone as
facts grow. Choose a fact domain that converges, such as one with finite
height.

Return `Unchanged` when merging adds no information, `Changed` when it does,
or `Conflict` when the facts cannot be combined. A conflict rejects that
union before graph mutation; it does not roll back an entire rewrite or
rebuild. Rebuilding a clean graph does no analysis work, so changes in external
state do not invalidate existing facts.

Read facts with `graph.analysis_data(id)`. See
[`analysis.hpp`](https://github.com/5yearsKim/c-egg/blob/main/include/eggc/analysis.hpp)
and the
[analysis tests](https://github.com/5yearsKim/c-egg/blob/main/tests/analysis_hooks_test.cpp)
for the contract and examples.
