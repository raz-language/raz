# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED RAZ_EXE OR NOT DEFINED SOURCE_ROOT OR NOT DEFINED WORK_ROOT)
  message(FATAL_ERROR "Owned I/O runtime requires RAZ_EXE, SOURCE_ROOT, WORK_ROOT")
endif()
include("${SOURCE_ROOT}/tests/cmake/copy_stdlib_closure.cmake")
file(REMOVE_RECURSE "${WORK_ROOT}")
file(MAKE_DIRECTORY "${WORK_ROOT}/src")
file(WRITE "${WORK_ROOT}/raz.toml" [=[
[package]
name = "passk_owned_io"
version = "1.0.0"
kind = "executable"
entry = "src/main.rz"
]=])
file(WRITE "${WORK_ROOT}/src/main.rz" [=[
import core::bytes;
import core::option;
import core::result;
import alloc::box;
import alloc::string;
import std::env::owned;
import std::process::owned;
import std::fs;
import std::fs::text;
import std::io::error;
fn main() -> i64 {
    usize path = alloc::box::allocate(9); i64 path_bytes[9] = [112,97,115,115,107,46,116,120,116];
    i64 index = 0; while (index < 9) { core::bytes::store_u8(path + index, path_bytes[index]); index += 1; }
    usize raw = alloc::box::allocate(5); i64 hello[5] = [104,101,108,108,111];
    index = 0; while (index < 5) { core::bytes::store_u8(raw + index, hello[index]); index += 1; }
    String value = alloc::string::from_bytes(raw, 5);
    Result<i64, IoError> written = std::fs::text::write_string(path, 9, &value);
    match written { Result<i64, IoError>::Ok(count) => { if (count != 5) { return 1; } } Result<i64, IoError>::Error(_) => { return 2; } }
    Result<String, TextError> loaded = std::fs::text::read_to_string(path, 9);
    match loaded { Result<String, TextError>::Ok(text) => { if (!alloc::string::equals_bytes(&text, raw, 5)) { return 3; } } Result<String, TextError>::Error(_) => { return 4; } }
    Result<String, EnvError> cwd = std::env::owned::current_dir_string();
    match cwd { Result<String, EnvError>::Ok(path_text) => { if (alloc::string::len(&path_text) <= 0) { return 5; } } Result<String, EnvError>::Error(_) => { return 6; } }
    Option<String> arg0 = std::process::owned::argument(0);
    match arg0 { Option<String>::Some(argument) => { if (alloc::string::len(&argument) <= 0) { return 7; } } Option<String>::None => { return 8; } }
    std::fs::remove(path, 9); alloc::box::deallocate(raw); alloc::box::deallocate(path); return 0;
}
]=])
raz_copy_stdlib_closure()
execute_process(COMMAND "${RAZ_EXE}" build "${WORK_ROOT}" --target test-host --force
  RESULT_VARIABLE build_result OUTPUT_VARIABLE build_output ERROR_VARIABLE build_error)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "Owned I/O build failed:\n${build_error}\n${build_output}")
endif()
if(WIN32)
  set(runtime_exe "${WORK_ROOT}/target/test-host/debug/passk_owned_io.exe")
else()
  set(runtime_exe "${WORK_ROOT}/target/test-host/debug/passk_owned_io")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env TERM=xterm "${runtime_exe}"
  WORKING_DIRECTORY "${WORK_ROOT}" RESULT_VARIABLE runtime_result OUTPUT_VARIABLE runtime_output ERROR_VARIABLE runtime_error)
if(NOT runtime_result EQUAL 0)
  message(FATAL_ERROR "Owned I/O runtime returned ${runtime_result}:\n${runtime_error}\n${runtime_output}")
endif()
