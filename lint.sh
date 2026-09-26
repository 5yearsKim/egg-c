#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
build_dir="${BUILD_DIR:-${root_dir}/build/lint}"
mode="fix"
run_tidy="false"

while (($#)); do
  case "$1" in
    --fix) mode="fix" ;;
    --check) mode="check" ;;
    --tidy) run_tidy="true" ;;
    -h|--help)
      cat <<'USAGE'
Usage: ./lint.sh [--fix | --check] [--tidy]

By default, applies clang-format to C++ files. --check checks formatting
without modifying files. --tidy additionally configures CMake and runs
clang-tidy on test and example translation units.
Set BUILD_DIR to choose the CMake build directory.
USAGE
      exit 0
      ;;
    *)
      printf 'Unknown option: %s\n' "$1" >&2
      printf 'Run ./lint.sh --help for usage.\n' >&2
      exit 2
      ;;
  esac
  shift
done

for tool in clang-format rg; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    printf 'Required tool not found: %s\n' "$tool" >&2
    exit 127
  fi
done

if [[ "$run_tidy" == "true" ]]; then
  for tool in cmake clang-tidy python3; do
    if ! command -v "$tool" >/dev/null 2>&1; then
      printf 'Required tool not found: %s\n' "$tool" >&2
      exit 127
    fi
  done
fi

mapfile -t cpp_files < <(
  cd "$root_dir"
  rg --files include tests examples benchmarks -g '*.cpp' -g '*.cc' -g '*.cxx' | sort
)
mapfile -t format_files < <(
  cd "$root_dir"
  rg --files include tests examples benchmarks -g '*.hpp' -g '*.h' -g '*.tpp' -g '*.cpp' -g '*.cc' -g '*.cxx' | sort
)

if ((${#cpp_files[@]} == 0)); then
  printf 'No C++ translation units found.\n' >&2
  exit 1
fi

if [[ "$mode" == "fix" ]]; then
  printf 'Applying clang-format...\n'
  clang-format -i "${format_files[@]/#/${root_dir}/}"
else
  printf 'Checking clang-format...\n'
  clang-format --dry-run --Werror "${format_files[@]/#/${root_dir}/}"
fi

if [[ "$run_tidy" == "true" ]]; then
  printf 'Configuring CMake compilation database in %s...\n' "$build_dir"
  cmake -S "$root_dir" -B "$build_dir" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DEGGC_BUILD_BENCHMARKS=ON

  printf 'Running clang-tidy...\n'
  # The consumer fixture is compiled by its separate installed-package project.
  # Use the parent compilation database rather than passing unconfigured sources.
  mapfile -t absolute_cpp_files < <(python3 - "$build_dir/compile_commands.json" <<'PYDB'
import json, sys
from pathlib import Path
commands = json.loads(Path(sys.argv[1]).read_text())
print("\n".join(sorted({str(Path(c["directory"], c["file"]).resolve()) for c in commands})))
PYDB
  )
  clang-tidy -p "$build_dir" --warnings-as-errors='*' "${absolute_cpp_files[@]}"
fi
