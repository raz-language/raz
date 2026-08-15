# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED RAZ_EXE OR NOT DEFINED SOURCE_ROOT OR NOT DEFINED WORK_ROOT)
  message(FATAL_ERROR "Runtime test requires RAZ_EXE, SOURCE_ROOT, and WORK_ROOT")
endif()

file(REMOVE_RECURSE "${WORK_ROOT}")
file(MAKE_DIRECTORY
  "${WORK_ROOT}/src/core/result"
  "${WORK_ROOT}/src/std/io"
  "${WORK_ROOT}/src/std/fs"
  "${WORK_ROOT}/src/std/net")

foreach(source_file
    library/core/result/result.rz
    library/std/io/error.rz
    library/std/fs/file.rz
    library/std/net/net.rz)
  string(REGEX REPLACE "^library/" "src/" destination "${source_file}")
  configure_file("${SOURCE_ROOT}/${source_file}" "${WORK_ROOT}/${destination}" COPYONLY)
endforeach()

file(WRITE "${WORK_ROOT}/raz.toml" [=[
[package]
name = "error_resources_runtime"
version = "1.0.0"
kind = "executable"
entry = "src/main.rz"
]=])

file(WRITE "${WORK_ROOT}/src/main.rz" [=[
import core::result;
import std::io::error;
import std::fs::file;
import std::net;

extern fn raz_rt_alloc(i64 size) -> usize;
extern fn raz_rt_dealloc(usize pointer);
extern fn raz_rt_store_u8(usize address, i64 value) -> i64;
extern fn raz_rt_load_u8(usize address) -> i64;
extern fn raz_rt_remove_one(usize path, i64 length) -> i64;

struct Guard {
    usize address;
}

impl Drop for Guard {
    fn drop(Guard&mut self) {
        raz_rt_store_u8(self.address, 55);
    }
}

fn fail() -> Result<i64, i64> {
    return Result<i64, i64>::Error(7);
}

fn propagate_cleanup(usize deferred, usize dropped) -> Result<bool, i64> {
    Guard guard = Guard { address: dropped };
    defer raz_rt_store_u8(deferred, 44);
    i64 value = fail()?;
    value;
    return Result<bool, i64>::Ok(true);
}

fn file_round_trip(usize path, usize data, usize output) -> Result<i64, IoError> {
    File file = try_open_with_flags(path, 18, read_flag | write_flag | create_flag | truncate_flag)?;
    bool complete = try_write_all(&mut file, data, 4)?;
    if (!complete) {
        return Result<i64, IoError>::Ok(10);
    }
    bool seeked = try_seek(&mut file, 0, 0)?;
    if (!seeked) {
        return Result<i64, IoError>::Ok(11);
    }
    i64 count = try_read(&mut file, output, 4)?;
    if (count != 4) {
        return Result<i64, IoError>::Ok(12);
    }
    return Result<i64, IoError>::Ok(0);
}

fn main() -> i64 {
    usize cleanup = raz_rt_alloc(2);
    raz_rt_store_u8(cleanup + 0, 0);
    raz_rt_store_u8(cleanup + 1, 0);
    Result<bool, i64> cleanup_result = propagate_cleanup(cleanup + 0, cleanup + 1);
    if (cleanup_result.is_ok()) {
        return 1;
    }
    if (raz_rt_load_u8(cleanup + 0) != 44) {
        return 2;
    }
    if (raz_rt_load_u8(cleanup + 1) != 55) {
        return 3;
    }
    raz_rt_dealloc(cleanup);

    usize path = raz_rt_alloc(18);
    i64 path_bytes[18] = [112, 97, 115, 115, 45, 105, 45, 114, 117, 110, 116, 105, 109, 101, 46, 98, 105, 110];
    i64 index = 0;
    while (index < 18) {
        raz_rt_store_u8(path + index, path_bytes[index]);
        index += 1;
    }
    usize data = raz_rt_alloc(4);
    usize output = raz_rt_alloc(4);
    raz_rt_store_u8(data + 0, 10);
    raz_rt_store_u8(data + 1, 20);
    raz_rt_store_u8(data + 2, 30);
    raz_rt_store_u8(data + 3, 40);

    Result<i64, IoError> io_result = file_round_trip(path, data, output);
    i64 status = 90;
    match io_result {
        Result<i64, IoError>::Ok(value) => {
            status = value;
        }
        Result<i64, IoError>::Error(_) => {
            status = 91;
        }
    }
    if (status != 0 || raz_rt_load_u8(output + 2) != 30) {
        return 4;
    }

    Result<TcpStream, IoError> tcp_error = try_tcp_stream_connect(0, 0, -1);
    if (!tcp_error.is_error()) {
        return 5;
    }
    Result<UdpSocket, IoError> udp_error = try_udp_socket_bind(-1);
    if (!udp_error.is_error()) {
        return 6;
    }

    raz_rt_remove_one(path, 18);
    raz_rt_dealloc(path);
    raz_rt_dealloc(data);
    raz_rt_dealloc(output);
    return 0;
}
]=])

execute_process(
  COMMAND "${RAZ_EXE}" build "${WORK_ROOT}" --target test-host --force
  RESULT_VARIABLE build_result
  OUTPUT_VARIABLE build_output
  ERROR_VARIABLE build_error)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "Error/resource project failed to build:\n${build_error}\n${build_output}")
endif()

if(WIN32)
  set(runtime_exe "${WORK_ROOT}/target/test-host/debug/error_resources_runtime.exe")
else()
  set(runtime_exe "${WORK_ROOT}/target/test-host/debug/error_resources_runtime")
endif()
if(NOT EXISTS "${runtime_exe}")
  message(FATAL_ERROR "Runtime executable was not produced: ${runtime_exe}")
endif()

execute_process(
  COMMAND "${runtime_exe}"
  WORKING_DIRECTORY "${WORK_ROOT}"
  RESULT_VARIABLE runtime_result
  OUTPUT_VARIABLE runtime_output
  ERROR_VARIABLE runtime_error)
if(NOT runtime_result EQUAL 0)
  message(FATAL_ERROR "Runtime returned ${runtime_result}:\n${runtime_error}\n${runtime_output}")
endif()
