# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED RAZ_EXE OR NOT DEFINED SOURCE_ROOT OR NOT DEFINED WORK_ROOT)
  message(FATAL_ERROR "Stdlib systems runtime requires RAZ_EXE, SOURCE_ROOT, WORK_ROOT")
endif()
include("${SOURCE_ROOT}/tests/cmake/copy_stdlib_closure.cmake")
file(REMOVE_RECURSE "${WORK_ROOT}")
file(MAKE_DIRECTORY "${WORK_ROOT}/src")
file(WRITE "${WORK_ROOT}/raz.toml" [=[
[package]
name = "stdlib_systems_runtime_fixture"
version = "1.0.0"
kind = "executable"
entry = "src/main.rz"
]=])
file(WRITE "${WORK_ROOT}/src/main.rz" [=[
import std::fs;
import std::log;
import std::net::reactor;


fn put(usize data, i64 index, i64 value) {
    unsafe { *((data + index)as u8*mut) = value as u8; }
}

fn main() -> i64 {
    BatchReactor reactor = std::net::reactor::batch_create(1);
    if (!std::net::reactor::batch_valid(&reactor)) { return 1; }
    i64 socket = 0;
    while (socket < 130) {
        if (!std::net::reactor::batch_watch_readable(&mut reactor, socket)) { return 2; }
        socket += 1;
    }
    if (std::net::reactor::batch_len(&reactor) != 130) { return 3; }
    if (!std::net::reactor::batch_set_interests(&mut reactor, 0, 2)) { return 13; }
    if (!std::net::reactor::batch_remove_swap(&mut reactor, 0)) { return 14; }
    if (std::net::reactor::batch_len(&reactor) != 129) { return 15; }
    if (!std::net::reactor::batch_schedule_after(&mut reactor, 42, 0)) { return 16; }
    if (std::net::reactor::batch_timer_len(&reactor) != 1) { return 17; }
    i64 timer_token = -1;
    if (!std::net::reactor::batch_pop_expired(&mut reactor, &mut timer_token)) { return 18; }
    if (timer_token != 42 || std::net::reactor::batch_timer_len(&reactor) != 0) { return 19; }
    if (!std::net::reactor::batch_schedule_after(&mut reactor, 7, 1000)) { return 20; }
    if (!std::net::reactor::batch_cancel_timer(&mut reactor, 7)) { return 21; }
    std::net::reactor::batch_destroy(&mut reactor);

    usize level = raz_rt_alloc(4); put(level,0,73); put(level,1,78); put(level,2,70); put(level,3,79);
    usize message = raz_rt_alloc(2); put(message,0,111); put(message,1,107);
    usize ready = raz_rt_alloc(5); put(ready,0,114); put(ready,1,101); put(ready,2,97); put(ready,3,100); put(ready,4,121);
    usize count = raz_rt_alloc(5); put(count,0,99); put(count,1,111); put(count,2,117); put(count,3,110); put(count,4,116);
    LogBuffer log = std::log::with_capacity(64);
    if (!std::log::begin(&mut log, level, 4)) { return 4; }
    if (!std::log::message(&mut log, message, 2)) { return 5; }
    if (!std::log::field_bool(&mut log, ready, 5, true)) { return 6; }
    if (!std::log::field_u64(&mut log, count, 5, 130 as u64)) { return 7; }
    if (!std::log::finish(&mut log) || std::log::len(&log) <= 0) { return 8; }

    usize root = raz_rt_alloc(21); i64 rb[21] = [114,97,122,45,115,121,115,116,101,109,115,45,116,101,115,116,45,114,111,111,116];
    usize child = raz_rt_alloc(27); i64 cb[27] = [114,97,122,45,115,121,115,116,101,109,115,45,116,101,115,116,45,114,111,111,116,47,97,47,98,47,99];
    i64 i = 0; while (i < 21) { put(root,i,rb[i]); i += 1; }
    i = 0; while (i < 27) { put(child,i,cb[i]); i += 1; }
    if (!std::fs::create_dir(child, 27)) { return 9; }
    if (!std::fs::is_dir(root, 21)) { return 10; }
    if (!std::fs::remove(root, 21)) { return 11; }
    if (std::fs::exists(root, 21)) { return 12; }
    raz_rt_dealloc(child); raz_rt_dealloc(root); raz_rt_dealloc(count); raz_rt_dealloc(ready); raz_rt_dealloc(message); raz_rt_dealloc(level);
    return 0;
}
]=])

raz_copy_stdlib_closure()

execute_process(COMMAND "${RAZ_EXE}" build "${WORK_ROOT}" --target test-host --profile debug --force
  RESULT_VARIABLE build_result OUTPUT_VARIABLE build_output ERROR_VARIABLE build_error)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "Stdlib systems build failed:\n${build_error}\n${build_output}")
endif()
if(WIN32)
  set(runtime_exe "${WORK_ROOT}/target/test-host/debug/stdlib_systems_runtime_fixture.exe")
else()
  set(runtime_exe "${WORK_ROOT}/target/test-host/debug/stdlib_systems_runtime_fixture")
endif()
execute_process(COMMAND "${runtime_exe}" WORKING_DIRECTORY "${WORK_ROOT}"
  RESULT_VARIABLE runtime_result OUTPUT_VARIABLE runtime_output ERROR_VARIABLE runtime_error)
if(NOT runtime_result EQUAL 0)
  message(FATAL_ERROR "Stdlib systems runtime returned ${runtime_result}:\n${runtime_error}\n${runtime_output}")
endif()
