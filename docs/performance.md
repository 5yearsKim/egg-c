# Measuring performance

Build the benchmarks in Release mode, run several samples, and compare the CSV
results using the same compiler, flags, machine, workload, and input size.

## Build and run

Run from the repository root:

```sh
cmake -S . -B /tmp/eggc-perf -DCMAKE_BUILD_TYPE=Release -DEGGC_BUILD_BENCHMARKS=ON
cmake --build /tmp/eggc-perf --target engine_benchmark advanced_benchmark --parallel 2
/tmp/eggc-perf/benchmarks/engine_benchmark all 100 3 > /tmp/eggc-engine.csv
/tmp/eggc-perf/benchmarks/advanced_benchmark all 2000 3 > /tmp/eggc-allocations.csv
```

Or run either benchmark with Bazel:

```sh
bazel run --config=release //benchmarks:engine_benchmark -- all 100 3
bazel run --config=release //benchmarks:advanced_benchmark -- all 2000 3
```

Both accept `[workload|all] [size] [repetitions]` and use deterministic inputs.
Replace `all` with a workload name to measure one operation:

| Benchmark | Workloads |
| --- | --- |
| `engine_benchmark` | `analysis_chain`, `congruence_cascade`, `repeated_variables`, `rewrite_growth`, `shared_expression`, `small_update` |
| `advanced_benchmark` | `matching_reuse`, `wide_dag`, `same_operator_update` |

`shared_expression` supports sizes up to 30; `all` caps it automatically.

## Compare results

- Check final node/class counts, extracted `cost`, and `stop_reason` before
  comparing engine timings. Rewrite workloads can stop at a limit before saturation.
- Compare `elapsed_ns` across repetitions. Engine setup is excluded, and
  `extraction_ns` is measured separately. `search_ns`, `apply_ns`, and `rebuild_ns`
  cover iteration work; they exclude the runner's initial rebuild.
- Use `analysis_evaluations`, `matches`, and `applications` to explain changes
  in engine work. The advanced benchmark reports `allocation_calls`,
  `requested_bytes`, `cost_calls`, and `copied_candidate_ids` for resource usage.
  Allocation counts exclude setup and warmup; requested bytes are not peak memory.
- Save separate CSV files before and after a change. Compare equivalent work
  across several samples, and avoid wall-clock thresholds in CI.

For diagnostics in your own code, inspect `graph.last_rebuild_stats()` for rebuild
work or enable `RunOptions::collect_rule_stats` for per-rule matches,
applications, and timings in `RunReport::history`.
