# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED RAZ_EXE OR NOT DEFINED SOURCE_ROOT OR NOT DEFINED WORK_ROOT)
  message(FATAL_ERROR "High-level application runtime requires RAZ_EXE, SOURCE_ROOT, WORK_ROOT")
endif()
include("${SOURCE_ROOT}/tests/cmake/copy_stdlib_closure.cmake")
file(REMOVE_RECURSE "${WORK_ROOT}")
file(MAKE_DIRECTORY "${WORK_ROOT}/src")
file(WRITE "${WORK_ROOT}/raz.toml" [=[
[package]
name = "high_level_application_runtime_fixture"
version = "1.0.0"
kind = "executable"
entry = "src/main.rz"
]=])
file(WRITE "${WORK_ROOT}/src/main.rz" [=[
import alloc::string;
import std::encoding::json;
import std::encoding::json::document;
import std::fmt;
import std::net;
import std::net::http::client;
import std::net::http::cookie;
import std::net::http::headers;
import std::net::http::server;
import std::thread;

fn ascii3(i64 a, i64 b, i64 c) -> String {
    String s = alloc::string::with_capacity(3);
    alloc::string::push_byte(&mut s, a);
    if (b >= 0) { alloc::string::push_byte(&mut s, b); }
    if (c >= 0) { alloc::string::push_byte(&mut s, c); }
    return s;
}

fn server_task(i64 listener_socket) -> FnOnce() -> i64 {
    return move fn() -> i64 {
        HttpServer server = HttpServer { listener: TcpListener { socket: listener_socket }, buffer_capacity: 4096, max_header_bytes: 65536, max_body_bytes: 1048576, timeout_millis: 3000 };
        ServerConnection connection = std::net::http::server::connection_empty();
        if (!std::net::http::server::accept(&server, &mut connection)) { return 41; }
        HttpRequest request = std::net::http::server::request_new();
        if (std::net::http::server::read_request(&mut connection, &mut request) != ServerError::None) { return 42; }
        if (!request.keep_alive || alloc::string::len(&request.method) != 3) { return 43; }

        HeaderBlock headers = std::net::http::headers::new();
        HeaderBlock trailers = std::net::http::headers::new();
        String trailer_name = ascii3(88,45,84); String trailer_value = ascii3(49,-1,-1);
        if (!std::net::http::headers::add(&mut trailers, alloc::string::data_ptr(&trailer_name), 3, alloc::string::data_ptr(&trailer_value), 1)) { return 44; }
        String reason = ascii3(79,75,-1);
        if (std::net::http::server::begin_chunked_response(&mut connection, 200, alloc::string::data_ptr(&reason), 2, &headers, true) != ServerError::None) { return 45; }
        String abc = ascii3(97,98,99); String def = ascii3(100,101,102);
        if (std::net::http::server::write_chunk(&mut connection, alloc::string::data_ptr(&abc), 3) != ServerError::None) { return 46; }
        if (std::net::http::server::write_chunk(&mut connection, alloc::string::data_ptr(&def), 3) != ServerError::None) { return 47; }
        if (std::net::http::server::finish_chunked_response(&mut connection, &trailers, true) != ServerError::None) { return 48; }

        if (std::net::http::server::read_request(&mut connection, &mut request) != ServerError::None) { return 49; }
        String ok = ascii3(111,107,-1);
        if (std::net::http::server::send_response(&mut connection, 200, alloc::string::data_ptr(&reason), 2, &headers, alloc::string::data_ptr(&ok), 2, false) != ServerError::None) { return 50; }
        return 0;
    };
}

fn main() -> i64 {
    // Flat JSON document: one owned source allocation, contiguous node arena,
    // escaped-key slow path only when lookup actually requires decoding.
    String source = alloc::string::with_capacity(64);
    i64 json_bytes[36] = [123,34,110,34,58,52,50,44,34,97,34,58,91,116,114,117,101,44,110,117,108,108,44,34,120,34,93,44,34,92,117,48,48,54,98,34];
    i64 i = 0; while (i < 36) { alloc::string::push_byte(&mut source, json_bytes[i]); i += 1; }
    alloc::string::push_byte(&mut source,58); alloc::string::push_byte(&mut source,49); alloc::string::push_byte(&mut source,125);
    JsonDocument document = std::encoding::json::document::empty();
    if (std::encoding::json::document::parse_owned(move source, 64, &mut document) != JsonError::None) { return 1; }
    if (std::encoding::json::document::node_count(&document) != 7) { return 2; }
    String scratch = alloc::string::with_capacity(16); String nkey = ascii3(110,-1,-1);
    i64 number = std::encoding::json::document::object_get(&document, std::encoding::json::document::root_index(&document), alloc::string::data_ptr(&nkey), 1, &mut scratch);
    i64 nvalue = 0; if (number < 0 || !std::encoding::json::document::number_i64(&document, number, &mut nvalue) || nvalue != 42) { return 3; }
    String kkey = ascii3(107,-1,-1);
    i64 escaped = std::encoding::json::document::object_get(&document, std::encoding::json::document::root_index(&document), alloc::string::data_ptr(&kkey), 1, &mut scratch);
    if (escaped < 0) { return 4; }

    // Compact cookie header update/replacement/removal and validated header use.
    CookieHeader cookies = std::net::http::cookie::new(); String id = ascii3(105,100,-1); String v42 = ascii3(52,50,-1);
    if (!std::net::http::cookie::set(&mut cookies, alloc::string::data_ptr(&id), 2, alloc::string::data_ptr(&v42), 2)) { return 5; }
    String xy = ascii3(120,121,-1); String zz = ascii3(122,122,-1);
    if (!std::net::http::cookie::set(&mut cookies, alloc::string::data_ptr(&xy), 2, alloc::string::data_ptr(&zz), 2) || std::net::http::cookie::len(&cookies) != 2) { return 6; }
    if (!std::net::http::cookie::remove(&mut cookies, alloc::string::data_ptr(&xy), 2) || std::net::http::cookie::len(&cookies) != 1) { return 7; }
    HeaderBlock request_headers = std::net::http::headers::new(); if (!std::net::http::cookie::add_to_headers(&cookies, &mut request_headers)) { return 8; }

    // High-level loopback HTTP: streaming chunked body with trailers followed
    // by a normal response on the same keep-alive connection.
    HttpServer server = std::net::http::server::bind(0, 8, 4096, 65536, 1048576, 3000);
    if (!std::net::http::server::valid(&server)) { return 9; }
    i64 port = std::net::http::server::local_port(&server); if (port <= 0) { return 10; }
    i64 listener_socket = server.listener.socket; server.listener.socket = -1;
    JoinHandleI64 thread = std::thread::spawn_callable(server_task(listener_socket));

    String url = alloc::string::with_capacity(64);
    i64 prefix[17] = [104,116,116,112,58,47,47,49,50,55,46,48,46,48,46,49,58]; i = 0; while (i < 17) { alloc::string::push_byte(&mut url,prefix[i]); i += 1; }
    std::fmt::append_i64(&mut url,port); alloc::string::push_byte(&mut url,47);
    HttpClient client = std::net::http::client::new(4096,65536,1048576,3000);
    // Use separate framing-safe headers for the wire request; cookie header is
    // separately validated above and intentionally not required by the server.
    HeaderBlock empty_headers = std::net::http::headers::new();
    HttpResponseHead head = std::net::http::client::response_head_new(); HttpBodyState state = std::net::http::client::body_state_new();
    if (std::net::http::client::begin_get(&mut client,alloc::string::data_ptr(&url),alloc::string::len(&url),&empty_headers,&mut head,&mut state) != ClientError::None) { return 11; }
    if (head.status != 200 || !head.chunked || !head.keep_alive) { return 12; }
    String body = alloc::string::with_capacity(8);
    while (!std::net::http::client::body_done(&state)) {
        if (!alloc::string::reserve(&mut body, alloc::string::len(&body) + 2)) { return 13; }
        usize tail = alloc::string::data_ptr(&body) + alloc::string::len(&body); i64 got = 0;
        if (std::net::http::client::read_body(&mut client,&mut state,tail,2,&mut got) != ClientError::None) { return 14; }
        if (got > 0) { body.length += got; }
    }
    if (alloc::string::len(&body) != 6 || alloc::string::byte_at(&body,0) != 97 || alloc::string::byte_at(&body,5) != 102) { return 15; }
    HttpResponse response = std::net::http::client::response_new();
    if (std::net::http::client::get(&mut client,alloc::string::data_ptr(&url),alloc::string::len(&url),&empty_headers,&mut response) != ClientError::None) { return 16; }
    if (response.status != 200 || alloc::string::len(&response.body) != 2) { return 17; }
    if (std::net::http::client::opened_connections(&client) != 1 || std::net::http::client::reused_connections(&client) != 1) { return 18; }
    i64 server_result = std::thread::join(&mut thread); if (server_result != 0) { return server_result; }
    return 0;
}
]=])
raz_copy_stdlib_closure()

execute_process(COMMAND "${RAZ_EXE}" build "${WORK_ROOT}" --target test-host --profile debug --force
  RESULT_VARIABLE build_result OUTPUT_VARIABLE build_output ERROR_VARIABLE build_error)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "High-level application build failed:\n${build_error}\n${build_output}")
endif()

set(native_root "${WORK_ROOT}/target/test-host/debug/native")
if(WIN32)
  file(GLOB module_objects "${native_root}/modules/*.obj")
  if(EXISTS "${native_root}/aggregate/package.obj")
    message(FATAL_ERROR "High-level generic application unexpectedly used aggregate native fallback")
  endif()
else()
  file(GLOB module_objects "${native_root}/modules/*.o")
  if(EXISTS "${native_root}/aggregate/package.o")
    message(FATAL_ERROR "High-level generic application unexpectedly used aggregate native fallback")
  endif()
endif()
list(LENGTH module_objects module_object_count)
if(module_object_count LESS 2)
  message(FATAL_ERROR "High-level generic application did not emit per-module native objects")
endif()
execute_process(COMMAND "${RAZ_EXE}" build "${WORK_ROOT}" --target test-host --profile debug --verbose
  RESULT_VARIABLE fresh_build_result OUTPUT_VARIABLE fresh_build_output ERROR_VARIABLE fresh_build_error)
if(NOT fresh_build_result EQUAL 0)
  message(FATAL_ERROR "High-level fresh rebuild failed:\n${fresh_build_error}\n${fresh_build_output}")
endif()
if(fresh_build_output MATCHES "Fallback")
  message(FATAL_ERROR "High-level fresh rebuild unexpectedly used aggregate fallback:\n${fresh_build_output}")
endif()
if(NOT fresh_build_output MATCHES "0 compiled")
  message(FATAL_ERROR "High-level fresh rebuild did not reuse all module compilation outputs:\n${fresh_build_output}")
endif()
if(NOT fresh_build_output MATCHES "Fresh[ ]+high_level_application_runtime_fixture native link")
  message(FATAL_ERROR "High-level fresh rebuild did not reuse native link output:\n${fresh_build_output}")
endif()
if(WIN32)
  set(runtime_exe "${WORK_ROOT}/target/test-host/debug/high_level_application_runtime_fixture.exe")
else()
  set(runtime_exe "${WORK_ROOT}/target/test-host/debug/high_level_application_runtime_fixture")
endif()
execute_process(COMMAND "${runtime_exe}" WORKING_DIRECTORY "${WORK_ROOT}"
  RESULT_VARIABLE runtime_result OUTPUT_VARIABLE runtime_output ERROR_VARIABLE runtime_error)
if(NOT runtime_result EQUAL 0)
  message(FATAL_ERROR "High-level application runtime returned ${runtime_result}:\n${runtime_error}\n${runtime_output}")
endif()
