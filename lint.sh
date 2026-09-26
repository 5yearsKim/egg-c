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

for tool in clang-format; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    printf 'Required tool not found: %s\n' "$tool" >&2
    exit 127
  fi
done

if [[ "$run_tidy" == "true" ]]; then
  for tool in cmake clang-tidy; do
    if ! command -v "$tool" >/dev/null 2>&1; then
      printf 'Required tool not found: %s\n' "$tool" >&2
      exit 127
    fi
  done
fi

mapfile -t cpp_files < <(
  cd "$root_dir"
  find include tests examples -type f \
    \( -name '*.cpp' -o -name '*.cc' -o -name '*.cxx' \) -print | sort
)
mapfile -t format_files < <(
  cd "$root_dir"
  find include tests examples -type f \
    \( -name '*.hpp' -o -name '*.h' -o -name '*.cpp' -o -name '*.cc' -o -name '*.cxx' \) -print | sort
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
  cmake -S "$root_dir" -B "$build_dir" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

  printf 'Running clang-tidy...\n'
  absolute_cpp_files=("${cpp_files[@]/#/${root_dir}/}")
  clang-tidy -p "$build_dir" --warnings-as-errors='*' "${absolute_cpp_files[@]}"
fi
