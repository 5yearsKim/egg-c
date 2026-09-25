# <img src="misc/images/eggc.png" height="32" alt="egg-c"> egg-c: egraphs good in C++

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.cppreference.com/w/cpp/17) [![GitHub stars](https://img.shields.io/github/stars/5yearsKim/c-egg)](https://github.com/5yearsKim/c-egg/stargazers) [![GitHub issues](https://img.shields.io/github/issues/5yearsKim/c-egg)](https://github.com/5yearsKim/c-egg/issues)

A C++17 implementation of e-graphs and equality saturation, inspired by Rust
[`egg`](https://github.com/egraphs-good/egg). This is an independent project,
not an official implementation of [`egg`](https://github.com/egraphs-good/egg).

Comments, questions, and open-source contributions are welcome. Feel free to
open an issue or pull request.

## Build and test

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Examples are disabled by default. Configure with `-DEGGC_BUILD_EXAMPLES=ON` to
build them. Example executables are placed in `build/examples/`. Test
executables are placed in `build/Testing/` and can be run through CTest as
shown above.

## Tutorials

Start with the [tutorial guide](docs/README.md), or follow the lessons in order:

1. [Getting started](docs/tutorial_getting_started.md): expressions, e-classes,
   pattern matching, and your first simplification.
2. [Practical usage](docs/basic_usage.md): constant folding, conditional rules,
   run limits, extraction costs, and troubleshooting.
3. [Equivalences and unsafe rewrites](docs/tutorial_explanations.md): diagnose
   incorrect equalities and understand rule preconditions.
