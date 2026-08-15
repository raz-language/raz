# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED RAZ_EXE OR NOT DEFINED SOURCE_ROOT OR NOT DEFINED WORK_ROOT)
  message(FATAL_ERROR "Callable runtime requires RAZ_EXE, SOURCE_ROOT, WORK_ROOT")
endif()
include("${SOURCE_ROOT}/tests/cmake/copy_stdlib_closure.cmake")
file(REMOVE_RECURSE "${WORK_ROOT}")
file(MAKE_DIRECTORY "${WORK_ROOT}/src")
file(WRITE "${WORK_ROOT}/raz.toml" [=[
[package]
name = "callable_runtime_fixture"
version = "1.0.0"
kind = "executable"
entry = "src/main.rz"
]=])
file(WRITE "${WORK_ROOT}/src/main.rz" [=[
import std::thread;
import std::thread::task;
import std::thread::future;
import std::thread::pool;
import std::thread::scheduler;

fn make_value(i64 base) -> FnOnce() -> i64 {
    return move fn() -> i64 { return base + 2; };
}

fn main() -> i64 {
    JoinHandleI64 handle = std::thread::spawn_callable(make_value(40));
    if (!std::thread::valid(&handle)) { return 1; }
    if (std::thread::join(&mut handle) != 42) { return 2; }

    TaskScopeI64 scope = std::thread::task::scope_create(4);
    if (!std::thread::task::scope_spawn_callable(&mut scope, make_value(50))) { return 3; }
    i64 joined = 0;
    unsafe { joined = std::thread::task::scope_join_all(&mut scope, 0, 0); }
    if (joined != 1) { return 4; }
    if (!std::thread::task::scope_destroy_empty(&mut scope)) { return 5; }

    WorkerPoolI64 raz_pool = std::thread::pool::create(2, 64);
    if (!std::thread::pool::valid(&raz_pool)) { return 8; }
    PoolTaskI64 raz_task = std::thread::pool::submit(&mut raz_pool, make_value(70), 1000);
    if (!std::thread::pool::task_valid(&raz_task)) { std::thread::pool::destroy(&mut raz_pool); return 9; }
    PoolTaskResultI64 raz_result = std::thread::pool::task_wait_millis(&raz_task, 1000);
    if (raz_result.status != 1 || raz_result.value != 72) {
        std::thread::pool::task_destroy(&mut raz_task);
        std::thread::pool::destroy(&mut raz_pool);
        return 10;
    }
    std::thread::pool::task_destroy(&mut raz_task);
    if (!std::thread::pool::wait_idle_millis(&raz_pool, 1000)) { std::thread::pool::destroy(&mut raz_pool); return 11; }
    std::thread::pool::destroy(&mut raz_pool);

    TimerSchedulerI64 scheduler = std::thread::scheduler::create(64);
    if (!std::thread::scheduler::valid(&scheduler)) { return 12; }
    TimerTaskI64 timer_task = std::thread::scheduler::after_i64(&mut scheduler, 2, 99);
    TimerTaskResultI64 timer_result = std::thread::scheduler::task_wait_millis(&timer_task, 1000);
    if (timer_result.status != 1 || timer_result.value != 99) {
        std::thread::scheduler::task_destroy(&mut timer_task);
        std::thread::scheduler::destroy(&mut scheduler);
        return 13;
    }
    std::thread::scheduler::task_destroy(&mut timer_task);
    TimerTaskI64 callable_timer = std::thread::scheduler::schedule(&mut scheduler, 2, make_value(100));
    TimerTaskResultI64 callable_result = std::thread::scheduler::task_wait_millis(&callable_timer, 1000);
    if (callable_result.status != 1 || callable_result.value != 102) {
        std::thread::scheduler::task_destroy(&mut callable_timer);
        std::thread::scheduler::destroy(&mut scheduler);
        return 14;
    }
    std::thread::scheduler::task_destroy(&mut callable_timer);
    std::thread::scheduler::destroy(&mut scheduler);
    return 0;
}
]=])
raz_copy_stdlib_closure()

execute_process(COMMAND "${RAZ_EXE}" build "${WORK_ROOT}" --target test-host --profile debug --force
  RESULT_VARIABLE build_result OUTPUT_VARIABLE build_output ERROR_VARIABLE build_error)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "Callable runtime build failed:\n${build_error}\n${build_output}")
endif()
if(WIN32)
  set(runtime_exe "${WORK_ROOT}/target/test-host/debug/callable_runtime_fixture.exe")
else()
  set(runtime_exe "${WORK_ROOT}/target/test-host/debug/callable_runtime_fixture")
endif()
execute_process(COMMAND "${runtime_exe}" WORKING_DIRECTORY "${WORK_ROOT}"
  RESULT_VARIABLE runtime_result OUTPUT_VARIABLE runtime_output ERROR_VARIABLE runtime_error)
if(NOT runtime_result EQUAL 0)
  message(FATAL_ERROR "Callable runtime returned ${runtime_result}:\n${runtime_error}\n${runtime_output}")
endif()
