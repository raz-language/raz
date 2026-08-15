# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED RAZ_EXE OR NOT DEFINED SOURCE_ROOT OR NOT DEFINED WORK_ROOT)
  message(FATAL_ERROR "Scheduler stress requires RAZ_EXE, SOURCE_ROOT, WORK_ROOT")
endif()
include("${SOURCE_ROOT}/tests/cmake/copy_stdlib_closure.cmake")
file(REMOVE_RECURSE "${WORK_ROOT}")
file(MAKE_DIRECTORY "${WORK_ROOT}/src")
file(WRITE "${WORK_ROOT}/raz.toml" [=[
[package]
name = "scheduler_stress_fixture"
version = "1.0.0"
kind = "executable"
entry = "src/main.rz"
]=])
file(WRITE "${WORK_ROOT}/src/main.rz" [=[
import std::thread;
import std::thread::pool;
import std::thread::scheduler;

fn sleep_value(i64 millis, i64 value) -> FnOnce() -> i64 {
    return move fn() -> i64 { std::thread::sleep_millis(millis); return value; };
}
fn value(i64 value) -> FnOnce() -> i64 { return move fn() -> i64 { return value; }; }

fn main() -> i64 {
    // Deterministic bounded-queue saturation and cancel-pending shutdown.
    WorkerPoolI64 pool = std::thread::pool::create(1, 1);
    if (!std::thread::pool::valid(&pool)) { return 1; }
    PoolTaskI64 first = std::thread::pool::submit(&mut pool, sleep_value(100, 11), 1000);
    std::thread::sleep_millis(10);
    if (std::thread::pool::active(&pool) != 1) { return 2; }
    PoolTaskI64 second = std::thread::pool::submit(&mut pool, value(22), 1000);
    PoolTaskI64 saturated = std::thread::pool::submit(&mut pool, value(33), 2);
    PoolTaskResultI64 saturated_result = std::thread::pool::task_wait(&saturated);
    if (saturated_result.status != -1) { return 3; }
    std::thread::pool::task_destroy(&mut saturated);

    std::thread::pool::shutdown(&mut pool, true);
    PoolTaskResultI64 second_result = std::thread::pool::task_wait(&second);
    if (second_result.status != -1) { return 4; }
    PoolTaskResultI64 first_result = std::thread::pool::task_wait_millis(&first, 1000);
    if (first_result.status != 1 || first_result.value != 11) { return 5; }
    std::thread::pool::task_destroy(&mut second);
    std::thread::pool::task_destroy(&mut first);
    std::thread::pool::destroy(&mut pool);

    // Earlier timer insertion must wake a worker already waiting on a later root.
    // Capacity overflow is terminal/cancelled rather than silently dropped.
    TimerSchedulerI64 scheduler = std::thread::scheduler::create(2);
    if (!std::thread::scheduler::valid(&scheduler)) { return 6; }
    TimerTaskI64 later = std::thread::scheduler::after_i64(&mut scheduler, 100, 100);
    TimerTaskI64 earlier = std::thread::scheduler::after_i64(&mut scheduler, 5, 5);
    TimerTaskI64 overflow = std::thread::scheduler::after_i64(&mut scheduler, 200, 200);
    TimerTaskResultI64 overflow_result = std::thread::scheduler::task_wait_millis(&overflow, 10);
    if (overflow_result.status != -1) { return 7; }
    TimerTaskResultI64 early_result = std::thread::scheduler::task_wait_millis(&earlier, 1000);
    if (early_result.status != 1 || early_result.value != 5) { return 8; }
    TimerTaskResultI64 late_early = std::thread::scheduler::task_wait_millis(&later, 5);
    if (late_early.status != 0) { return 9; }
    std::thread::scheduler::shutdown(&mut scheduler, true);
    TimerTaskResultI64 late_cancel = std::thread::scheduler::task_wait_millis(&later, 100);
    if (late_cancel.status != -1) { return 10; }
    std::thread::scheduler::task_destroy(&mut overflow);
    std::thread::scheduler::task_destroy(&mut earlier);
    std::thread::scheduler::task_destroy(&mut later);
    std::thread::scheduler::destroy(&mut scheduler);
    return 0;
}
]=])
raz_copy_stdlib_closure()

execute_process(COMMAND "${RAZ_EXE}" build "${WORK_ROOT}" --target test-host --profile debug --force
  RESULT_VARIABLE build_result OUTPUT_VARIABLE build_output ERROR_VARIABLE build_error)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "Scheduler stress build failed:\n${build_error}\n${build_output}")
endif()
if(WIN32)
  set(runtime_exe "${WORK_ROOT}/target/test-host/debug/scheduler_stress_fixture.exe")
else()
  set(runtime_exe "${WORK_ROOT}/target/test-host/debug/scheduler_stress_fixture")
endif()
execute_process(COMMAND "${runtime_exe}" WORKING_DIRECTORY "${WORK_ROOT}"
  RESULT_VARIABLE runtime_result OUTPUT_VARIABLE runtime_output ERROR_VARIABLE runtime_error)
if(NOT runtime_result EQUAL 0)
  message(FATAL_ERROR "Scheduler stress returned ${runtime_result}:\n${runtime_error}\n${runtime_output}")
endif()
