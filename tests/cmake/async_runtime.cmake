# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED RAZ_EXE OR NOT DEFINED SOURCE_ROOT OR NOT DEFINED WORK_ROOT)
  message(FATAL_ERROR "Async runtime requires RAZ_EXE, SOURCE_ROOT, WORK_ROOT")
endif()
include("${SOURCE_ROOT}/tests/cmake/copy_stdlib_closure.cmake")
file(REMOVE_RECURSE "${WORK_ROOT}")
file(MAKE_DIRECTORY "${WORK_ROOT}/src")
file(WRITE "${WORK_ROOT}/raz.toml" [=[
[package]
name = "async_runtime_fixture"
version = "1.0.0"
kind = "executable"
entry = "src/main.rz"
]=])
file(WRITE "${WORK_ROOT}/src/main.rz" [=[
import std::thread::cancellation;
import std::thread::timer;
import std::thread::task;

fn main() -> i64 {
    CancellationToken token = std::thread::cancellation::create();
    if (!std::thread::cancellation::valid(&token)) { return 1; }
    if (std::thread::cancellation::requested(&token)) { return 2; }
    if (std::thread::cancellation::wait_millis(&token, 1)) { return 3; }
    std::thread::cancellation::request(&mut token);
    if (!std::thread::cancellation::requested(&token)) { return 4; }
    if (!std::thread::cancellation::wait_millis(&token, 0)) { return 5; }

    Deadline deadline = std::thread::timer::after_millis(2);
    if (std::thread::timer::expired(&deadline)) { return 6; }
    std::thread::timer::sleep_until(&deadline);
    if (!std::thread::timer::expired(&deadline)) { return 7; }

    TaskScopeI64 scope = std::thread::task::scope_create(8);
    if (!std::thread::task::scope_valid(&scope) || std::thread::task::scope_count(&scope) != 0) { return 8; }
    std::thread::task::scope_cancel(&mut scope);
    if (!std::thread::task::scope_cancelled(&scope)) { return 9; }
    return 0;
}
]=])
raz_copy_stdlib_closure()

execute_process(COMMAND "${RAZ_EXE}" build "${WORK_ROOT}" --target test-host --profile debug --force
  RESULT_VARIABLE build_result OUTPUT_VARIABLE build_output ERROR_VARIABLE build_error)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "Async runtime build failed:\n${build_error}\n${build_output}")
endif()
if(WIN32)
  set(runtime_exe "${WORK_ROOT}/target/test-host/debug/async_runtime_fixture.exe")
else()
  set(runtime_exe "${WORK_ROOT}/target/test-host/debug/async_runtime_fixture")
endif()
execute_process(COMMAND "${runtime_exe}" WORKING_DIRECTORY "${WORK_ROOT}"
  RESULT_VARIABLE runtime_result OUTPUT_VARIABLE runtime_output ERROR_VARIABLE runtime_error)
if(NOT runtime_result EQUAL 0)
  message(FATAL_ERROR "Async runtime returned ${runtime_result}:\n${runtime_error}\n${runtime_output}")
endif()
