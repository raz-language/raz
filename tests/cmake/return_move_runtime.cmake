# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED RAZ_EXE OR NOT DEFINED WORK_ROOT)
  message(FATAL_ERROR "Return-move runtime requires RAZ_EXE and WORK_ROOT")
endif()
file(REMOVE_RECURSE "${WORK_ROOT}")
file(MAKE_DIRECTORY "${WORK_ROOT}/src")
file(WRITE "${WORK_ROOT}/raz.toml" [=[
[package]
name = "passk_return_move"
version = "1.0.0"
kind = "executable"
entry = "src/main.rz"
]=])
file(WRITE "${WORK_ROOT}/src/main.rz" [=[
extern fn raz_compiler_rt_arena_create(i64 count) -> i64;
extern fn raz_compiler_rt_arena_destroy(i64 handle);
extern fn raz_compiler_rt_arena_get(i64 handle, i64 index) -> i64;
extern fn raz_compiler_rt_arena_set(i64 handle, i64 index, i64 value);

struct Owner { i64 tracker; }
impl Drop for Owner {
    fn drop(Owner&mut self) {
        i64 count = raz_compiler_rt_arena_get(self.tracker, 0);
        raz_compiler_rt_arena_set(self.tracker, 0, count + 1);
    }
}

fn make_owner(i64 tracker) -> Owner {
    Owner value = Owner(tracker);
    return value;
}

fn exercise(i64 tracker) -> i64 {
    Owner value = make_owner(tracker);
    if (raz_compiler_rt_arena_get(tracker, 0) != 0) { return 1; }
    return 0;
}

fn main() -> i64 {
    i64 tracker = raz_compiler_rt_arena_create(1);
    if (tracker == 0) { return 2; }
    i64 result = exercise(tracker);
    if (result != 0) { return result; }
    if (raz_compiler_rt_arena_get(tracker, 0) != 1) { return 3; }
    raz_compiler_rt_arena_destroy(tracker);
    return 0;
}
]=])
execute_process(COMMAND "${RAZ_EXE}" build "${WORK_ROOT}" --target test-host --force
  RESULT_VARIABLE build_result OUTPUT_VARIABLE build_output ERROR_VARIABLE build_error)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "Return-move build failed:\n${build_error}\n${build_output}")
endif()
if(WIN32)
  set(runtime_exe "${WORK_ROOT}/target/test-host/debug/passk_return_move.exe")
else()
  set(runtime_exe "${WORK_ROOT}/target/test-host/debug/passk_return_move")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env TERM=xterm "${runtime_exe}"
  RESULT_VARIABLE runtime_result OUTPUT_VARIABLE runtime_output ERROR_VARIABLE runtime_error)
if(NOT runtime_result EQUAL 0)
  message(FATAL_ERROR "Return-move runtime returned ${runtime_result}:\n${runtime_error}\n${runtime_output}")
endif()
