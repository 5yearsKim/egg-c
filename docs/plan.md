**Your next step should be a correctness milestone: add regression tests, fix pattern matching, and make the runner search only rebuilt graphs.** You already have the main components of an equality-saturation engine, but the current matcher can miss valid rewrites.

I reviewed all headers, implementations, and the example, and compared the design with Rust `egg`’s documentation. The project builds cleanly with `-Wall -Wextra -Wpedantic`; the example produces:

```text
simplified: x
iterations: 2, nodes: 5
```

CTest reports no tests. I also ran temporary diagnostic programs outside the repository; I did not change your project files.

Your current implementation covers:

| Component | Current state |
|---|---|
| E-nodes and hash-consing | String operators, child IDs, structural hashing |
| Union-find | Path compression and union by rank |
| Congruence closure | Full-graph rebuilding until no further merges |
| Pattern matching | Variables and recursive patterns, but incomplete search |
| Rewriting | RHS instantiation and equality merging |
| Runner | Iteration and node limits |
| Extraction | Minimum AST-size cost |
| Library infrastructure | C++17/CMake; no test suite or substantive documentation |

The module boundaries are a useful foundation. I would keep them and develop the project in the following order.

1. **Establish correctness tests and graph invariants.**

   Add CTest integration and separate tests for the graph, matcher, runner, and extractor.

   Start with these graph properties:

   - Adding the same canonical e-node twice returns equivalent IDs.
   - Merging classes preserves all their nodes after rebuilding.
   - If `a == b`, rebuilding makes `f(a) == f(b)`.
   - Congruence propagates through multiple parent levels.
   - Rebuilding twice has the same result as rebuilding once.
   - Old IDs remain usable after merges.
   - Cycles such as `a == f(a)` are supported.

   Add an internal debug invariant checker. After rebuilding, it should verify that child IDs are canonical, nodes belong to representative classes, and the memo table agrees with class storage.

   **Define the clean/dirty contract explicitly.** In [egraph.cpp](/home/onion/Projects/egg-c/src/egraph.cpp:29), `merge()` changes union-find but leaves nodes in their old storage until `rebuild()`. Consequently, queries can see incomplete class contents before rebuilding.

   I confirmed that merging two of three leaf classes makes `node_count()` report `2`, although three nodes remain stored.

   This deferred maintenance can be a valid design, but callers need clear rules. Rust `egg` also distinguishes clean and dirty graphs and requires rebuilding before queries that depend on restored invariants. See its [EGraph documentation](https://docs.rs/egg/latest/egg/struct.EGraph.html).

   **Completion criterion:** graph invariants have tests, and each public operation documents whether it works on a dirty graph.

2. **Fix complete e-matching—the most urgent algorithmic change.**

   In [pattern.cpp](/home/onion/Projects/egg-c/src/pattern.cpp:12), `unify()` returns on its first successful alternative. `match()` therefore returns at most one substitution.

   I reproduced two failures:

   | Case | Expected | Actual |
   |---|---:|---:|
   | An e-class contains `f(a)` and `f(b)`; match `(f ?x)` | 2 substitutions | 1 |
   | Match `(pair (f ?x) ?x)` against a pair whose first child contains both `f(a)` and `f(b)` and whose second child is `b` | 1 substitution | 0 |

   The second failure happens because choosing `a` for the first child prevents the second child from matching, and the matcher never revisits that choice.

   Implement matching as **enumeration of compatible substitutions**:

   - A variable either creates a binding or checks an existing binding using canonical IDs.
   - An operator pattern examines every matching e-node in the class.
   - For each child, carry forward every compatible partial substitution.
   - If a later child fails, continue exploring earlier alternatives.
   - Canonicalize and deduplicate complete substitutions.

   Keep the first implementation simple: a recursive matcher returning vectors is adequate. Optimize allocation and compile patterns later.

   Add tests for repeated variables, nested alternatives, arity mismatches, variables at the root, and matching cyclic graphs with finite patterns.

   **Completion criterion:** both reproduced failures pass, and small hand-enumerated examples return exactly the expected substitution sets.

3. **Restructure the runner around search → apply → rebuild.**

   In [runner.cpp](/home/onion/Projects/egg-c/src/runner.cpp:6), matching and mutation are interleaved. Later searches see a graph whose class contents have not been rebuilt.

   I also reproduced premature saturation on dirty input:

   ```text
   Add a, b, c
   Merge a and b without rebuilding
   Run rule b → c

   Actual: reports Saturated, but a and c remain unequal
   ```

   Use this iteration structure:

   ```text
   validate rules
   rebuild input graph

   repeat:
       search all rules on the clean graph
       retain matched class IDs and substitutions
       apply the collected matches
       rebuild the graph
       record progress and check termination
   ```

   This does not require copying the graph: simply finish searching before mutation begins.

   Include these changes:

   - Validate that every RHS variable appears in the LHS before running. Currently, an invalid rule throws during instantiation and can already have added nodes.
   - Base saturation on actual progress, including any progress introduced during rebuilding.
   - Document zero-iteration behavior and node-limit boundary behavior.
   - Check resource budgets during expensive work. The current node check happens only after an entire iteration.
   - Introduce a `RunOptions` struct as configuration grows.
   - Record per-iteration matches, successful unions, node/class counts, and search/apply/rebuild durations.
   - Add time and match limits; report truncation as a limit, never as saturation.

   Rust `egg`’s runner provides time limits, iteration reporting, and scheduling; these are useful reference points for your API. [Runner documentation](https://docs.rs/egg/latest/egg/struct.Runner.html)

   **Completion criterion:** the dirty-input reproduction passes, returned graphs are rebuilt, and every stop reason has a regression test.

4. **Refactor extraction to compute costs once.**

   In [extract.cpp](/home/onion/Projects/egg-c/src/extract.cpp:41), reconstructing each child calls `extract()` again. Each call repeats the cost computation across the whole graph.

   Split extraction into two operations:

   - Compute the best cost and chosen e-node for each e-class.
   - Reconstruct an expression using that existing table.

   An `Extractor` object can retain the table and support multiple roots. Initially, its lifetime should require an unchanged, clean graph.

   Then add:

   - A result containing both expression and cost.
   - A configurable cost function, retaining AST size as the default.
   - Tests for cyclic classes with finite representatives, shared subexpressions, overflow, and deep expressions.
   - Explicit cost-function requirements so custom costs do not invalidate convergence or reconstruction.

   Rust `egg` similarly separates an extractor from its cost function. [Extractor documentation](https://docs.rs/egg/latest/egg/struct.Extractor.html)

   **Completion criterion:** one extraction analysis serves all requested roots, and reconstruction never reruns that analysis.

5. **Add expression parsing and e-class analysis.**

   Once the core is reliable, these provide the largest practical expansion.

   First, introduce an expression representation independent of the e-graph, ideally a flat node vector whose children refer to earlier entries. Add:

   - S-expression parsing and printing.
   - `add_expr()`.
   - Pattern parsing.
   - Rewrite construction with early validation.

   This makes examples and regression cases much easier to write:

   ```cpp
   auto input = parse_expr("(* (+ x 0) 1)");
   auto rule = rewrite("add-zero", "(+ ?x 0)", "?x");
   ```

   Then implement per-e-class analysis. Follow the conceptual operations of `egg`: create data from an e-node, merge class data, propagate changes, and optionally add derived equalities. Constant folding is a good first application. [Analysis documentation](https://docs.rs/egg/latest/egg/trait.Analysis.html)

   Use this end-to-end acceptance case:

   ```text
   (+ (* 2 3) (+ x 0))  →  (+ 6 x)
   ```

   Specify numeric semantics and overflow behavior. Conflicting constant facts must be detected rather than silently choosing a value.

   Analysis propagation must reach a fixed point even when data changes without a new class union.

   **Completion criterion:** constants propagate through parents after merges, and extraction selects the folded result.

6. **Optimize rebuilding and matching using measurements.**

   Your [current rebuild](/home/onion/Projects/egg-c/src/egraph.cpp:37) scans all stored classes repeatedly. Keep this simple implementation until the correctness suite is strong; it can serve as a reference for an incremental implementation.

   Establish benchmarks for deep congruence chains, many unrelated classes, and associative/commutative rewrite workloads. Measure phases separately.

   Then prioritize:

   - Parent-use lists and a worklist of affected parents for rebuilding.
   - An operator/arity index to reduce candidate classes during matching.
   - Interned operators and variable IDs if profiling shows string overhead.
   - Scheduling/backoff for rules generating excessive matches.

   Validate incremental rebuilding against the full-scan version using randomized add/merge sequences.

   Also create small differential tests against a **pinned Rust `egg` version**, comparing equivalence and extraction costs. Do not compare internal IDs or require identical expressions when multiple equal-cost answers exist.

   **Completion criterion:** measured performance improves while semantic tests continue to agree.

I would make the **first implementation milestone** just steps 1–3: tests, complete matching, and a runner with an explicit rebuild boundary. That addresses confirmed correctness failures and gives you a dependable foundation for extraction improvements and analysis. Generic language templates, proof explanations, advanced extraction, and compiled matching can follow once this core is trustworthy.