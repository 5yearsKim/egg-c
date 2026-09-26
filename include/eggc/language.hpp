#pragma once
#if !defined(__cpp_concepts) || __cpp_concepts < 201907L
#error "egg-c requires C++20 or newer; compile consumers with -std=c++20"
#endif
#include <concepts>
#include <cstddef>
#include <functional>
#include <ranges>

#include "id.hpp"

namespace eggc {
template <class T>
concept Hashable = requires(const T &value) {
  { std::hash<T>{}(value) } -> std::convertible_to<std::size_t>;
};

// Accessors may return containers by reference or borrowed views such as span.
// Owning temporaries are rejected: child references must survive accessor
// calls.
template <class R>
concept ChildRange =
    std::ranges::random_access_range<R> && std::ranges::sized_range<R> &&
    std::ranges::common_range<R> && std::ranges::borrowed_range<R> &&
    requires(R children, std::size_t index) {
      { children.size() } -> std::convertible_to<std::size_t>;
      children[index];
    };
template <class R>
concept ConstChildren =
    ChildRange<R> &&
    std::same_as<std::ranges::range_reference_t<R>, const Id &> &&
    requires(R children, std::size_t index) {
      { children[index] } -> std::same_as<const Id &>;
    };
template <class R>
concept MutableChildren =
    ChildRange<R> && std::same_as<std::ranges::range_reference_t<R>, Id &> &&
    requires(R children, std::size_t index) {
      { children[index] } -> std::same_as<Id &>;
    };

// L is the application's complete node type. These constraints check its API;
// they cannot prove semantic equality, stable hashes, or matching soundness.
// matches() ignores child IDs; equality/hash include them. Matching nodes must
// have equal discriminants. Identity stays immutable while a node is stored.
template <class L>
concept Language =
    std::copyable<L> && std::equality_comparable<L> &&
    requires(const L &node, const L &other, L &mutable_node) {
      typename L::Discriminant;
      requires std::copyable<typename L::Discriminant>;
      requires std::equality_comparable<typename L::Discriminant>;
      requires Hashable<typename L::Discriminant>;
      { node.discriminant() } -> std::same_as<typename L::Discriminant>;
      { node.children() } -> ConstChildren;
      { mutable_node.children_mut() } -> MutableChildren;
      { node.matches(other) } -> std::same_as<bool>;
      { node.hash() } -> std::same_as<std::size_t>;
    };

template <Language L> struct NodeHash {
  std::size_t operator()(const L &node) const { return node.hash(); }
};
inline void hash_combine(std::size_t &seed, std::size_t value) {
  seed ^= value + 0x9e3779b9u + (seed << 6) + (seed >> 2);
}
} // namespace eggc
