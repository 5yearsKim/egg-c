# egg-c

A C++20 e-graph library inspired by [egg](https://github.com/egraphs-good/egg).
The application defines its node type outside this package.
The `Language` and `AnalysisFor` concepts check the required interfaces at
compile time. The engine stores
that type directly and does not depend on an operator vocabulary, TensorLang,
or MLIR.

```cpp
using Graph = eggc::EGraph<MyNode, MyAnalysis>;
using Expr = eggc::RecExpr<MyNode>;
using Pattern = eggc::Pattern<MyNode>;
using Rewrite = eggc::Rewrite<MyNode, MyAnalysis>;
using Extractor = eggc::Extractor<MyNode, MyAnalysis>;
```

`MyAnalysis` is optional: `EGraph<MyNode>` uses `NoAnalysis<MyNode>`.
See [the custom-language guide](docs/custom_languages.md) for the complete node
contract, pattern construction, typed analysis, and custom rewrite callbacks.
The [external-language regression test](tests/language_test.cpp) is a working
example that requires no TensorLang or MLIR dependencies.

## Build and test

```sh
cmake -S . -B /tmp/eggc-build
cmake --build /tmp/eggc-build
ctest --test-dir /tmp/eggc-build --output-on-failure
```

From this package's standalone Bazel workspace:

```sh
bazel build //:eggc
bazel test //tests:all
```

CMake propagates the C++20 requirement through its interface target. Standalone
Bazel uses `-std=c++20` from `.bazelrc`. Other Bazel consumers must enable C++20
for their own source files; a header-only dependency cannot propagate `copts`.
In the parent XLA workspace, use `--config=joint_shard`: it selects C++20 only for
`research/joint_shard/` sources and uses the hermetic GCC 12 / glibc 2.35 sysroot
needed for C++20 library headers. Dependencies retain C++17 source flags and use
that sysroot too. Other XLA builds retain their defaults. These Linux binaries
require glibc 2.35 or newer to run.

The CMake target is an interface library. Generic algorithms are defined in
headers and included `.tpp` files so external node types can instantiate them.
Concrete language methods, analysis, and application rewrites can be implemented
in the application's `.cpp` files. Empty engine `.cpp` placeholders have been
removed.

The previous string parser, default string language, arithmetic analysis,
operator-variable matching, tree-expression API, and related demo/benchmark/
differential tooling have been removed. This interface intentionally breaks
compatibility with that API.
