# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED RAZ_EXE OR NOT DEFINED SOURCE_ROOT OR NOT DEFINED WORK_ROOT)
  message(FATAL_ERROR "HTTP zero-copy runtime requires RAZ_EXE, SOURCE_ROOT, WORK_ROOT")
endif()
include("${SOURCE_ROOT}/tests/cmake/copy_stdlib_closure.cmake")
file(REMOVE_RECURSE "${WORK_ROOT}")
file(MAKE_DIRECTORY "${WORK_ROOT}/src")
file(WRITE "${WORK_ROOT}/raz.toml" [=[
[package]
name = "http_zero_copy_runtime_fixture"
version = "1.0.0"
kind = "executable"
entry = "src/main.rz"
]=])
file(WRITE "${WORK_ROOT}/src/main.rz" [=[
import alloc::string;
import core::bytes;
import std::io::buffer;
import std::net::http;
import std::net::http::server;

fn empty_view() -> HttpRequestView {
    return HttpRequestView {
        source: BytesView { data: 0, length: 0 },
        method: BytesView { data: 0, length: 0 },
        target: BytesView { data: 0, length: 0 },
        version: BytesView { data: 0, length: 0 },
        header_block: BytesView { data: 0, length: 0 },
        body: BytesView { data: 0, length: 0 },
        content_length: -1,
        chunked: false,
        connection_close: false,
    };
}

fn main() -> i64 {
    i64 raw[101] = [71,69,84,32,47,111,110,101,32,72,84,84,80,47,49,46,49,13,10,72,111,115,116,58,32,120,13,10,67,111,110,116,101,110,116,45,76,101,110,103,116,104,58,32,51,13,10,13,10,97,98,99,71,69,84,32,47,116,119,111,32,72,84,84,80,47,49,46,49,13,10,72,111,115,116,58,32,120,13,10,67,111,110,116,101,110,116,45,76,101,110,103,116,104,58,32,48,13,10,13,10];
    String request_bytes = alloc::string::with_capacity(101);
    i64 index = 0;
    while (index < 101) {
        if (!alloc::string::push_byte(&mut request_bytes, raw[index])) { return 1; }
        index += 1;
    }
    ServerConnection connection = std::net::http::server::connection_empty();
    connection.open = true;
    if (!byte_buffer_append(&mut connection.input, alloc::string::data_ptr(&request_bytes), 101)) { return 1; }
    usize first_base = byte_buffer_data(&connection.input);
    HttpRequestView first = empty_view();
    if (std::net::http::server::read_request_view(&mut connection, &mut first) != ServerError::None) { return 2; }
    if (!std::net::http::server::request_pending(&connection)) { return 3; }
    if (first.method.data != first_base || first.method.length != 3) { return 4; }
    if (first.target.length != 4 || core::bytes::byte_at(first.target, 1) != 111) { return 5; }
    if (first.body.length != 3 || core::bytes::byte_at(first.body, 0) != 97) { return 6; }
    if (!std::net::http::server::release_request(&mut connection)) { return 7; }
    if (!std::net::http::server::buffered_request_ready(&connection)) { return 8; }
    usize second_base = byte_buffer_data(&connection.input);
    HttpRequestView second = empty_view();
    if (std::net::http::server::read_request_view(&mut connection, &mut second) != ServerError::None) { return 9; }
    if (second.method.data != second_base || second.target.length != 4 || core::bytes::byte_at(second.target, 1) != 116) { return 10; }
    if (!std::net::http::server::release_request(&mut connection)) { return 11; }
    if (byte_buffer_readable(&connection.input) != 0) { return 12; }
    connection.open = false;
    return 0;
}
]=])
raz_copy_stdlib_closure()
execute_process(COMMAND "${RAZ_EXE}" build "${WORK_ROOT}" --target test-host --profile debug --force
  RESULT_VARIABLE build_result OUTPUT_VARIABLE build_output ERROR_VARIABLE build_error)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "HTTP zero-copy build failed:\n${build_error}\n${build_output}")
endif()
if(WIN32)
  set(runtime_exe "${WORK_ROOT}/target/test-host/debug/http_zero_copy_runtime_fixture.exe")
else()
  set(runtime_exe "${WORK_ROOT}/target/test-host/debug/http_zero_copy_runtime_fixture")
endif()
execute_process(COMMAND "${runtime_exe}" WORKING_DIRECTORY "${WORK_ROOT}"
  RESULT_VARIABLE runtime_result OUTPUT_VARIABLE runtime_output ERROR_VARIABLE runtime_error)
if(NOT runtime_result EQUAL 0)
  message(FATAL_ERROR "HTTP zero-copy runtime returned ${runtime_result}:\n${runtime_error}\n${runtime_output}")
endif()
