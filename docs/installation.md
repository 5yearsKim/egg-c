# Installation

egg-c is a header-only library. You need a **C++20 compiler** and, for the
commands below, **CMake 3.16 or newer**. There is no compiled library to link.

## Build and try it

```sh
git clone https://github.com/5yearsKim/c-egg.git
cd c-egg
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
./build/examples/tutorial_getting_started
```

Prefer Bazel? From the checkout, run:

```sh
bazel test //tests:all //examples:all
bazel run //examples:tutorial_getting_started
```

Use the Bazel version listed in
[`.bazelversion`](https://github.com/5yearsKim/c-egg/blob/main/.bazelversion).

## Use it in your project

Place the checkout at `third_party/egg-c`. In your existing `CMakeLists.txt`,
add egg-c and link your application's target (`my_app` here):

```cmake
add_subdirectory(third_party/egg-c)
target_link_libraries(my_app PRIVATE eggc::eggc)
```

The target supplies the include path and C++20 requirement. To skip egg-c's
examples and tests, configure with `-DEGGC_BUILD_EXAMPLES=OFF -DBUILD_TESTING=OFF`.

Include the library in your C++ code:

```cpp
#include <eggc/all.hpp>
```

You can also compile directly from the checkout:

```sh
c++ -std=c++20 -Iinclude main.cpp -o /tmp/my-optimizer
/tmp/my-optimizer
```

Keep the entire `include/eggc/` directory, including `impl/` and `detail/`.
External Bazel projects must also enable C++20 in their own build settings.

Next, write your first optimizer in [Getting started](01_getting_started.md).
