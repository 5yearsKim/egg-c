#!/bin/sh
set -eu
cpp_binary=$1
cargo_binary=$2
rust_project=$3
cpp_result=$("$cpp_binary")
rust_result=$("$cargo_binary" run --quiet --manifest-path "$rust_project/Cargo.toml")
if [ "$cpp_result" != "$rust_result" ]; then
    printf 'C++ result: %s\nRust egg result: %s\n' "$cpp_result" "$rust_result" >&2
    exit 1
fi
printf '%s\n' "$cpp_result"
