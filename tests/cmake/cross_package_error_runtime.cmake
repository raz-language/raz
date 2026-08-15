# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED RAZ_EXE OR NOT DEFINED SOURCE_ROOT OR NOT DEFINED WORK_ROOT)
  message(FATAL_ERROR "Cross-package test requires RAZ_EXE, SOURCE_ROOT, and WORK_ROOT")
endif()

file(REMOVE_RECURSE "${WORK_ROOT}")
file(MAKE_DIRECTORY
  "${WORK_ROOT}/safeio/src/core/result"
  "${WORK_ROOT}/safeio/src/std/io"
  "${WORK_ROOT}/safeio/src/std/fs"
  "${WORK_ROOT}/consumer/src")
foreach(source_file
    library/core/result/result.rz
    library/std/io/error.rz
    library/std/fs/file.rz)
  string(REGEX REPLACE "^library/" "safeio/src/" destination "${source_file}")
  configure_file("${SOURCE_ROOT}/${source_file}" "${WORK_ROOT}/${destination}" COPYONLY)
endforeach()
file(WRITE "${WORK_ROOT}/safeio/src/api.rz" [=[
namespace api;
public import core::result;
public import std::io::error;
public import std::fs::file;
]=])
file(WRITE "${WORK_ROOT}/safeio/raz.toml" [=[
[package]
name = "error_safeio"
version = "1.0.0"
kind = "static-library"
entry = "src/api.rz"
]=])
file(WRITE "${WORK_ROOT}/consumer/raz.toml" [=[
[package]
name = "error_cross_consumer"
version = "1.0.0"
kind = "executable"
entry = "src/main.rz"

[dependencies]
safeio = "../safeio"
]=])
file(WRITE "${WORK_ROOT}/consumer/src/main.rz" [=[
import safeio::api;
extern fn raz_rt_alloc(i64 size) -> usize;
extern fn raz_rt_dealloc(usize pointer);
extern fn raz_rt_store_u8(usize address, i64 value) -> i64;
extern fn raz_rt_remove_one(usize path, i64 length) -> i64;

fn open_and_flush(usize path) -> Result<bool, IoError> {
    File file = try_open_with_flags(path, 16, read_flag | write_flag | create_flag | truncate_flag)?;
    return try_flush(&mut file);
}

fn main() -> i64 {
    usize path = raz_rt_alloc(16);
    i64 bytes[16] = [112, 97, 115, 115, 45, 105, 45, 99, 114, 111, 115, 115, 46, 98, 105, 110];
    i64 index = 0;
    while (index < 16) {
        raz_rt_store_u8(path + index, bytes[index]);
        index += 1;
    }
    Result<bool, IoError> result = open_and_flush(path);
    if (result.is_error()) {
        return 1;
    }
    raz_rt_remove_one(path, 16);
    raz_rt_dealloc(path);
    return 0;
}
]=])

execute_process(
  COMMAND "${RAZ_EXE}" build "${WORK_ROOT}/consumer" --target test-host --force
  RESULT_VARIABLE build_result OUTPUT_VARIABLE build_output ERROR_VARIABLE build_error)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "Cross-package error API failed to build:\n${build_error}\n${build_output}")
endif()
if(WIN32)
  set(runtime_exe "${WORK_ROOT}/consumer/target/test-host/debug/error_cross_consumer.exe")
else()
  set(runtime_exe "${WORK_ROOT}/consumer/target/test-host/debug/error_cross_consumer")
endif()
execute_process(
  COMMAND "${runtime_exe}"
  WORKING_DIRECTORY "${WORK_ROOT}/consumer"
  RESULT_VARIABLE runtime_result OUTPUT_VARIABLE runtime_output ERROR_VARIABLE runtime_error)
if(NOT runtime_result EQUAL 0)
  message(FATAL_ERROR "Cross-package runtime returned ${runtime_result}:\n${runtime_error}\n${runtime_output}")
endif()
