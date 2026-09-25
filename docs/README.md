# Learn `egg-c`

`egg-c` is a C++17 library for exploring equivalent expressions and choosing a
low-cost one. These guides assume basic C++ knowledge; no e-graph experience is
needed. Read them in order:

1. [Getting started](tutorial_getting_started.md): build an example, learn
   e-classes and patterns, and simplify an expression.
2. [Practical usage](basic_usage.md): combine constant folding with rewrites,
   add conditions, and control runs and extraction.
3. [Equivalences and unsafe rewrites](tutorial_explanations.md): understand how
   incorrect rules lead to incorrect equalities.

The workflow is: **parse → add to an e-graph → apply rules and rebuild → extract**.
The practical guide also covers [troubleshooting](basic_usage.md#troubleshooting),
[differential verification](basic_usage.md#differential-verification-against-rust-egg),
and [benchmarks](basic_usage.md#benchmarks).
