# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED RAZ OR NOT DEFINED WORK)
  message(FATAL_ERROR "RAZ and WORK are required")
endif()
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}/src")
file(WRITE "${WORK}/raz.toml" [=[
[package]
name = "forge-aggregate-roundtrip-fixture"
version = "0.1.0"
kind = "executable"
source = "src"
entry = "src/main.rz"
]=])
file(WRITE "${WORK}/src/main.rz" [=[
struct Inner {
    i64 value;
}

struct Outer {
    Inner inner;
}

fn read_inner(Inner input) -> i64 {
    return input.value;
}

// Keep a slice identity in the emitted aggregate table so the project path
// must print and reparse a symbol such as @i64[].
fn slice_count(i64[] values) -> i64 {
    return values.length;
}

fn main() -> i64 {
    Inner inner = Inner(0);
    Outer outer = Outer(inner);
    return read_inner(outer.inner);
}
]=])
execute_process(
  COMMAND "${RAZ}" run "${WORK}" --target test-host --force
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "aggregate/slice Forge round-trip executable failed (${result}):\n${output}\n${error}")
endif()
