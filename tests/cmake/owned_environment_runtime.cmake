# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED RAZ_EXE OR NOT DEFINED SOURCE_ROOT OR NOT DEFINED WORK_ROOT)
  message(FATAL_ERROR "Owned environment runtime requires RAZ_EXE, SOURCE_ROOT, WORK_ROOT")
endif()
include("${SOURCE_ROOT}/tests/cmake/copy_stdlib_closure.cmake")
file(REMOVE_RECURSE "${WORK_ROOT}")
file(MAKE_DIRECTORY "${WORK_ROOT}/src")
file(WRITE "${WORK_ROOT}/raz.toml" [=[
[package]
name = "owned_env_process_runtime_fixture"
version = "1.0.0"
kind = "executable"
entry = "src/main.rz"
]=])
file(WRITE "${WORK_ROOT}/src/main.rz" [=[
import alloc::string;
import core::option;
import core::result;
import std::env::owned;
import std::env::path;
import std::path::buf;
import std::process::args;

fn main() -> i64 {
    Result<PathBuf, EnvError> cwd_result = std::env::path::current_dir();
    match cwd_result {
        Result<PathBuf, EnvError>::Error(_) => { return 1; }
        Result<PathBuf, EnvError>::Ok(cwd) => {
            PathBuf current = move cwd;
            if (current.is_empty()) { return 2; }
        }
    }
    Args process_args = std::process::args::args();
    if (std::process::args::remaining(&process_args) <= 0) { return 3; }
    Option<String> first = std::process::args::next(&mut process_args);
    match first {
        Option<String>::None => { return 4; }
        Option<String>::Some(value) => { String arg0 = move value; if (alloc::string::len(&arg0) <= 0) { return 5; } }
    }
    return 0;
}
]=])
raz_copy_stdlib_closure()
execute_process(COMMAND "${RAZ_EXE}" build "${WORK_ROOT}" --target test-host --profile debug --force
  RESULT_VARIABLE build_result OUTPUT_VARIABLE build_output ERROR_VARIABLE build_error)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "Owned environment build failed:\n${build_error}\n${build_output}")
endif()
if(WIN32)
  set(runtime_exe "${WORK_ROOT}/target/test-host/debug/owned_env_process_runtime_fixture.exe")
else()
  set(runtime_exe "${WORK_ROOT}/target/test-host/debug/owned_env_process_runtime_fixture")
endif()
execute_process(COMMAND "${runtime_exe}"
  RESULT_VARIABLE runtime_result OUTPUT_VARIABLE runtime_output ERROR_VARIABLE runtime_error)
if(NOT runtime_result EQUAL 0)
  message(FATAL_ERROR "Owned environment runtime returned ${runtime_result}:\n${runtime_error}\n${runtime_output}")
endif()
