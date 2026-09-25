#!/usr/bin/env python3
"""Replay seeded e-graph cases against egg-c and pinned Rust egg.

The input protocol is tab-separated. Queries emit one line each; class IDs and
equal-cost expression choices are never compared.
"""

import argparse
import difflib
import os
from pathlib import Path
import random
import re
import subprocess
import sys


QUERY_OPS = {"eq", "cost", "fact", "match", "run", "witness"}


def core_case(seed):
    rng = random.Random(seed)
    lines = ["version\t1"]
    terms = {}
    depth = {}
    for name in "abcd":
        lines.append(f"add\t{name}\t{name}")
        terms[name] = name
        depth[name] = 0
    names = list(terms)
    for index in range(18):
        candidates = [name for name in names if depth[name] < 3]
        left = rng.choice(candidates)
        op = rng.choice(("f", "g", "pair"))
        if op == "pair":
            right = rng.choice(candidates)
            expression = f"(pair {terms[left]} {terms[right]})"
            new_depth = 1 + max(depth[left], depth[right])
        else:
            expression = f"({op} {terms[left]})"
            new_depth = depth[left] + 1
        handle = f"n{index}"
        lines.append(f"add\t{handle}\t{expression}")
        terms[handle] = expression
        depth[handle] = new_depth
        names.append(handle)
        if index % 3 == 1:
            a, b = rng.sample(names, 2)
            lines.append(f"merge\t{a}\t{b}")
        if index % 4 == 3:
            lines.extend(checkpoint(rng, names))
    lines.extend(checkpoint(rng, names))
    lines.extend((
        "rule\tunwrap-f\t(f ?x)\t?x\t-",
        "rule\tunwrap-g\t(g ?x)\t?x\t-",
        "run\t32",
    ))
    lines.extend(checkpoint(rng, names))
    return "\n".join(lines) + "\n"


def checkpoint(rng, names):
    left, right = rng.sample(names, 2)
    root = rng.choice(names)
    return [
        "rebuild",
        f"eq\t{left}\t{right}",
        f"cost\t{root}\tsize",
        f"cost\t{root}\tdepth",
        f"witness\t{root}",
        f"match\t{root}\t?x",
        f"match\t{root}\t(f ?x)",
        f"match\t{root}\t(pair ?x ?x)",
    ]


def arithmetic_case(seed):
    rng = random.Random(seed ^ 0xA611)
    a = rng.randint(-7, 7)
    b = rng.randint(-7, 7)
    op = rng.choice(("+", "*"))
    expression = f"({op} {a} {b})"
    late = rng.choice((True, False))
    if late:
        operand = "x"
        setup = ["add\tx\tx", f"rule\tx-is-value\tx\t{expression}\t-"]
    else:
        operand = expression
        setup = [f"add\tterm\t{expression}"]
    fact_handle = "x" if late else "term"
    lines = [
        "version\t1", *setup,
        "add\tone\t1",
        f"add\troot\t(/ {operand} {operand})",
        "rebuild", "eq\troot\tone", "cost\troot\tsize",
        f"fact\t{fact_handle}",
        "match\troot\t(/ ?x ?x)", "witness\troot",
        "rule\tdiv-self\t(/ ?x ?x)\t1\tnonzero:?x",
        "run\t16", "eq\troot\tone", "cost\troot\tsize",
        f"fact\t{fact_handle}",
    ]
    return "\n".join(lines) + "\n", (a + b if op == "+" else a * b) != 0


def execute(binary, source):
    try:
        return subprocess.run(
            [str(binary)], input=source, text=True, capture_output=True, timeout=15,
            check=False,
        )
    except subprocess.TimeoutExpired as error:
        raise RuntimeError(f"{binary} timed out") from error


def first_difference(cpp, rust):
    if cpp.returncode != 0 or rust.returncode != 0:
        return "process-error", 0
    left = cpp.stdout.splitlines()
    right = rust.stdout.splitlines()
    for index in range(max(len(left), len(right))):
        a = left[index] if index < len(left) else "<missing>"
        b = right[index] if index < len(right) else "<missing>"
        if a != b:
            return a.split("\t", 1)[0], index
    return None


def invariant_failure(source, output):
    queries = [line.split("\t", 1)[0] for line in source.splitlines()
               if line.split("\t", 1)[0] in QUERY_OPS]
    results = output.splitlines()
    if len(queries) != len(results):
        return "output-count", min(len(queries), len(results))
    for index, (query, result) in enumerate(zip(queries, results)):
        if result.split("\t", 1)[0] != query:
            return "output-order", index
        if query == "witness" and result != "witness\t1":
            return "invalid-witness", index
        if query == "run" and result != "run\tsat":
            return "unsaturated-run", index
    return None


def valid_reduction(candidate, cpp_binary, rust_binary, category):
    cpp = execute(cpp_binary, candidate)
    rust = execute(rust_binary, candidate)
    difference = first_difference(cpp, rust)
    return cpp.returncode == 0 and rust.returncode == 0 and difference and difference[0] == category


def reduce_case(source, cpp_binary, rust_binary, category):
    lines = source.splitlines()
    if category == "process-error":
        return source
    chunk = max(1, (len(lines) - 1) // 2)
    while chunk:
        index = 1  # Keep the version line.
        while index < len(lines):
            trial = lines[:index] + lines[index + chunk:]
            candidate = "\n".join(trial) + "\n"
            if valid_reduction(candidate, cpp_binary, rust_binary, category):
                lines = trial
            else:
                index += chunk
        chunk //= 2
    for index, line in enumerate(lines):
        if not line.startswith("add\t"):
            continue
        parts = line.split("\t")
        inner = re.fullmatch(r"\((?:f|g) ([^()]*)\)", parts[2])
        if inner:
            trial = lines.copy()
            trial[index] = f"add\t{parts[1]}\t{inner.group(1)}"
            if valid_reduction("\n".join(trial) + "\n", cpp_binary, rust_binary, category):
                lines = trial
                line = trial[index]
        for small in ("0", "1", "-1", "a"):
            trial_line = re.sub(r"(?<![\w])-?\d+(?![\w])", small, line)
            if trial_line == line:
                continue
            trial = lines.copy()
            trial[index] = trial_line
            if valid_reduction("\n".join(trial) + "\n", cpp_binary, rust_binary, category):
                lines = trial
                break
    return "\n".join(lines) + "\n"


def compare(source, label, cpp_binary, rust_binary, expected_final=None):
    cpp = execute(cpp_binary, source)
    rust = execute(rust_binary, source)
    difference = first_difference(cpp, rust)
    if not difference:
        difference = invariant_failure(source, cpp.stdout)
    if not difference and expected_final is not None:
        eq_lines = [line for line in cpp.stdout.splitlines() if line.startswith("eq\t")]
        if not eq_lines or eq_lines[-1] != f"eq\t{int(expected_final)}":
            difference = ("expected-equivalence", len(cpp.stdout.splitlines()) - 1)
    if not difference:
        return
    category, output_index = difference
    query_lines = [(index + 1, line) for index, line in enumerate(source.splitlines())
                   if line.split("\t", 1)[0] in QUERY_OPS]
    query_location = query_lines[output_index] if output_index < len(query_lines) else None
    prefix = Path.cwd() / "differential_failure"
    prefix.with_suffix(".case").write_text(source)
    prefix.with_suffix(".cpp.out").write_text(cpp.stdout + cpp.stderr)
    prefix.with_suffix(".rust.out").write_text(rust.stdout + rust.stderr)
    if category not in ("process-error", "expected-equivalence", "output-count",
                        "output-order", "invalid-witness", "unsaturated-run"):
        reduced = reduce_case(source, cpp_binary, rust_binary, category)
        prefix.with_suffix(".min.case").write_text(reduced)
    diff = "".join(difflib.unified_diff(
        cpp.stdout.splitlines(keepends=True), rust.stdout.splitlines(keepends=True),
        fromfile="C++", tofile="Rust egg"))
    raise AssertionError(
        f"{label}: first divergent output {output_index + 1} ({category}), "
        f"input query {query_location}; "
        f"replay {prefix.with_suffix('.case')}\n{diff}\n"
        f"C++ exit={cpp.returncode}, stderr={cpp.stderr}\n"
        f"Rust exit={rust.returncode}, stderr={rust.stderr}"
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("cpp_binary", type=Path)
    parser.add_argument("cargo_binary", type=Path)
    parser.add_argument("rust_project", type=Path)
    parser.add_argument("--seed", type=lambda value: int(value, 0), default=0xE66)
    parser.add_argument("--cases", type=int, default=20)
    parser.add_argument("--replay", type=Path)
    args = parser.parse_args()
    if args.cases < 0:
        parser.error("--cases must be nonnegative")
    manifest = args.rust_project / "Cargo.toml"
    subprocess.run([str(args.cargo_binary), "build", "--locked", "--quiet",
                    "--manifest-path", str(manifest)], check=True)
    target_dir = Path(os.environ.get("CARGO_TARGET_DIR", args.rust_project / "target"))
    rust_binary = target_dir / "debug" / "eggc-egg-differential"
    if args.replay:
        compare(args.replay.read_text(), str(args.replay), args.cpp_binary, rust_binary)
        return
    directory = Path(__file__).parent
    for fixture, expected in (("smoke.case", None), ("nonzero.case", True),
                              ("zero.case", False), ("overflow.case", False)):
        compare((directory / fixture).read_text(), fixture, args.cpp_binary, rust_binary,
                expected)
    for index in range(args.cases):
        seed = args.seed + index
        compare(core_case(seed), f"core seed {seed}", args.cpp_binary, rust_binary)
        source, expected = arithmetic_case(seed)
        compare(source, f"arithmetic seed {seed}", args.cpp_binary, rust_binary, expected)
    print(f"differential checks passed: 4 fixtures and {2 * args.cases} seeded cases")


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, RuntimeError) as error:
        print(error, file=sys.stderr)
        sys.exit(1)
