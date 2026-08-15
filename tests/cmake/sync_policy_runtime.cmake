# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED RAZ_EXE OR NOT DEFINED SOURCE_ROOT OR NOT DEFINED WORK_ROOT OR NOT DEFINED PYTHON_EXECUTABLE)
  message(FATAL_ERROR "sync policy runtime requires RAZ_EXE, SOURCE_ROOT, WORK_ROOT, PYTHON_EXECUTABLE")
endif()
include("${SOURCE_ROOT}/tests/cmake/copy_stdlib_closure.cmake")
file(REMOVE_RECURSE "${WORK_ROOT}")
file(MAKE_DIRECTORY "${WORK_ROOT}/src")
file(WRITE "${WORK_ROOT}/raz.toml" [=[
[package]
name = "sync_policy_runtime_fixture"
version = "1.0.0"
kind = "executable"
entry = "src/main.rz"
]=])
file(WRITE "${WORK_ROOT}/src/main.rz" [=[
import alloc::box;
import core::atomic;
import std::sync::barrier;
import std::sync::once;
import std::sync::semaphore;
import std::thread;
import std::thread::latch;

fn pointer_valid(i8*mut value) -> bool {
    usize raw = 0;
    unsafe { raw = value as usize; }
    return raw != 0;
}

fn latch_wait_address(usize raw) -> i64 {
    unsafe {
        if (std::thread::latch::latch_wait_millis(raw as i8*mut, 1000)) { return 1; }
    }
    return 0;
}

fn barrier_wait_address(usize raw) -> i64 {
    unsafe {
        if (std::sync::barrier::barrier_wait(raw as i8*mut)) { return 1; }
    }
    return 0;
}

fn once_call_address(usize raw, usize counter) -> i64 {
    unsafe {
        return std::sync::once::once_call_i64(raw as i8*mut, move fn() -> i64 {
            core::atomic::fetch_add_i64(counter, 1);
            return 77;
        });
    }
}

fn main() -> i64 {
    auto latch = std::thread::latch::latch_create(2);
    if (!pointer_valid(latch) || std::thread::latch::latch_remaining(latch) != 2) { return 1; }
    if (std::thread::latch::latch_wait_millis(latch, 1)) { return 2; }
    usize waiter_latch = 0; unsafe { waiter_latch = latch as usize; }
    JoinHandleI64 waiter = std::thread::spawn_callable(move fn() -> i64 { return latch_wait_address(waiter_latch); });
    std::thread::latch::latch_count_down(latch, 1);
    if (std::thread::latch::latch_remaining(latch) != 1) { return 3; }
    std::thread::latch::latch_count_down(latch, 5);
    if (std::thread::join(&mut waiter) != 1 || std::thread::latch::latch_remaining(latch) != 0) { return 4; }
    std::thread::latch::latch_wait(latch);
    std::thread::latch::latch_destroy(latch);

    auto semaphore = std::sync::semaphore::semaphore_create(1, 2);
    if (!pointer_valid(semaphore) || std::sync::semaphore::semaphore_available(semaphore) != 1) { return 5; }
    if (!std::sync::semaphore::semaphore_try_acquire(semaphore)) { return 6; }
    if (std::sync::semaphore::semaphore_acquire_millis(semaphore, 1)) { return 7; }
    if (std::sync::semaphore::semaphore_release(semaphore, 3) != 2) { return 8; }
    if (!std::sync::semaphore::semaphore_acquire(semaphore)) { return 9; }
    if (!std::sync::semaphore::semaphore_try_acquire(semaphore)) { return 10; }
    if (std::sync::semaphore::semaphore_try_acquire(semaphore)) { return 11; }
    std::sync::semaphore::semaphore_destroy(semaphore);

    auto barrier = std::sync::barrier::barrier_create(3);
    if (!pointer_valid(barrier) || std::sync::barrier::barrier_parties(barrier) != 3) { return 12; }
    usize barrier_a = 0; unsafe { barrier_a = barrier as usize; }
    usize barrier_b = 0; unsafe { barrier_b = barrier as usize; }
    JoinHandleI64 first = std::thread::spawn_callable(move fn() -> i64 { return barrier_wait_address(barrier_a); });
    JoinHandleI64 second = std::thread::spawn_callable(move fn() -> i64 { return barrier_wait_address(barrier_b); });
    i64 winners = 0;
    if (std::sync::barrier::barrier_wait(barrier)) { winners += 1; }
    winners += std::thread::join(&mut first);
    winners += std::thread::join(&mut second);
    if (winners != 1) { return 13; }

    usize barrier_c = 0; unsafe { barrier_c = barrier as usize; }
    usize barrier_d = 0; unsafe { barrier_d = barrier as usize; }
    JoinHandleI64 third = std::thread::spawn_callable(move fn() -> i64 { return barrier_wait_address(barrier_c); });
    JoinHandleI64 fourth = std::thread::spawn_callable(move fn() -> i64 { return barrier_wait_address(barrier_d); });
    winners = 0;
    if (std::sync::barrier::barrier_wait(barrier)) { winners += 1; }
    winners += std::thread::join(&mut third);
    winners += std::thread::join(&mut fourth);
    std::sync::barrier::barrier_destroy(barrier);
    if (winners != 1) { return 14; }

    auto once = std::sync::once::once_create();
    if (!pointer_valid(once)) { return 15; }
    usize once_raw = 0; unsafe { once_raw = once as usize; }
    usize counter = alloc::box::allocate_aligned(8, 8);
    if (counter == 0) { return 16; }
    core::atomic::store_i64(counter, 0);
    JoinHandleI64 once_first = std::thread::spawn_callable(move fn() -> i64 { return once_call_address(once_raw, counter); });
    JoinHandleI64 once_second = std::thread::spawn_callable(move fn() -> i64 { return once_call_address(once_raw, counter); });
    i64 first_value = std::thread::join(&mut once_first);
    i64 second_value = std::thread::join(&mut once_second);
    if (first_value != 77 || second_value != 77 || core::atomic::load_i64(counter) != 1 || !std::sync::once::once_completed(once)) { return 17; }
    i64 repeated = std::sync::once::once_call_i64(once, move fn() -> i64 { core::atomic::fetch_add_i64(counter, 1); return 99; });
    if (repeated != 77 || core::atomic::load_i64(counter) != 1) { return 18; }
    std::sync::once::once_destroy(once);
    alloc::box::deallocate_aligned(counter, 8);
    return 0;
}
]=])
raz_copy_stdlib_closure()
execute_process(COMMAND "${RAZ_EXE}" build "${WORK_ROOT}" --target test-host --profile debug --force
  RESULT_VARIABLE build_result OUTPUT_VARIABLE build_output ERROR_VARIABLE build_error)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "sync policy build failed:\n${build_error}\n${build_output}")
endif()
if(WIN32)
  set(runtime_exe "${WORK_ROOT}/target/test-host/debug/sync_policy_runtime_fixture.exe")
else()
  set(runtime_exe "${WORK_ROOT}/target/test-host/debug/sync_policy_runtime_fixture")
endif()
execute_process(COMMAND "${runtime_exe}" RESULT_VARIABLE runtime_result OUTPUT_VARIABLE runtime_output ERROR_VARIABLE runtime_error)
if(NOT runtime_result EQUAL 0)
  message(FATAL_ERROR "sync policy runtime returned ${runtime_result}:\n${runtime_error}\n${runtime_output}")
endif()
