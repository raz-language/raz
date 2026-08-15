# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED RAZ_EXE OR NOT DEFINED SOURCE_ROOT OR NOT DEFINED WORK_ROOT)
  message(FATAL_ERROR "Network/codec runtime requires RAZ_EXE, SOURCE_ROOT, WORK_ROOT")
endif()
file(REMOVE_RECURSE "${WORK_ROOT}")
file(MAKE_DIRECTORY "${WORK_ROOT}/src")
set(network_codec_sources
  library/core/result/result.rz
  library/core/bytes/bytes.rz
  library/alloc/box/box.rz
  library/alloc/vec/vec.rz
  library/alloc/string/string.rz
  library/std/io/error.rz
  library/std/io/buffer/buffer.rz
  library/std/net/net.rz
  library/std/net/address/address.rz
  library/std/net/typed/typed.rz
  library/std/net/buffered/buffered.rz
  library/std/net/framed/framed.rz
  library/std/encoding/binary/binary.rz)
foreach(source_file IN LISTS network_codec_sources)
  string(REGEX REPLACE "^library/" "src/" destination "${source_file}")
  get_filename_component(destination_dir "${WORK_ROOT}/${destination}" DIRECTORY)
  file(MAKE_DIRECTORY "${destination_dir}")
  configure_file("${SOURCE_ROOT}/${source_file}" "${WORK_ROOT}/${destination}" COPYONLY)
endforeach()
file(WRITE "${WORK_ROOT}/raz.toml" [=[
[package]
name = "passl_network_codec"
version = "1.0.0"
kind = "executable"
entry = "src/main.rz"
]=])
file(WRITE "${WORK_ROOT}/src/main.rz" [=[
import core::result;
import core::bytes;
import alloc::box;
import alloc::string;
import std::io::error;
import std::io::buffer;
import std::net;
import std::net::address;
import std::net::typed;
import std::net::buffered;
import std::net::framed;
import std::encoding::binary;

fn check_codec() -> i64 {
    usize memory = alloc::box::allocate(128);
    if (memory == 0) { return 1; }
    BinaryWriter writer = std::encoding::binary::writer(memory, 128);
    match std::encoding::binary::write_u16_be(&mut writer, 0x1234 as u16) {
        Result<bool, CodecError>::Ok(_) => {}
        Result<bool, CodecError>::Error(_) => { alloc::box::deallocate(memory); return 2; }
    }
    match std::encoding::binary::write_u32_le(&mut writer, 0x89abcdef as u32) {
        Result<bool, CodecError>::Ok(_) => {}
        Result<bool, CodecError>::Error(_) => { alloc::box::deallocate(memory); return 3; }
    }
    match std::encoding::binary::write_var_u64(&mut writer, 300) {
        Result<bool, CodecError>::Ok(_) => {}
        Result<bool, CodecError>::Error(_) => { alloc::box::deallocate(memory); return 4; }
    }
    match std::encoding::binary::write_var_i64(&mut writer, -42) {
        Result<bool, CodecError>::Ok(_) => {}
        Result<bool, CodecError>::Error(_) => { alloc::box::deallocate(memory); return 5; }
    }
    i64 length = std::encoding::binary::writer_position(&writer);
    BinaryReader reader = std::encoding::binary::reader(memory, length);
    match std::encoding::binary::read_u16_be(&mut reader) {
        Result<u16, CodecError>::Ok(value) => { if (value != (0x1234 as u16)) { alloc::box::deallocate(memory); return 6; } }
        Result<u16, CodecError>::Error(_) => { alloc::box::deallocate(memory); return 7; }
    }
    match std::encoding::binary::read_u32_le(&mut reader) {
        Result<u32, CodecError>::Ok(value) => { if (value != (0x89abcdef as u32)) { alloc::box::deallocate(memory); return 8; } }
        Result<u32, CodecError>::Error(_) => { alloc::box::deallocate(memory); return 9; }
    }
    match std::encoding::binary::read_var_u64(&mut reader) {
        Result<u64, CodecError>::Ok(value) => { if (value != 300) { alloc::box::deallocate(memory); return 10; } }
        Result<u64, CodecError>::Error(_) => { alloc::box::deallocate(memory); return 11; }
    }
    match std::encoding::binary::read_var_i64(&mut reader) {
        Result<i64, CodecError>::Ok(value) => { if (value != -42) { alloc::box::deallocate(memory); return 12; } }
        Result<i64, CodecError>::Error(_) => { alloc::box::deallocate(memory); return 13; }
    }
    // Malformed tenth varint byte must be rejected rather than overflowing.
    i64 index = 0; while (index < 10) { core::bytes::store_u8(memory + index, 128); index += 1; }
    core::bytes::store_u8(memory + 9, 2);
    BinaryReader malformed = std::encoding::binary::reader(memory, 10);
    match std::encoding::binary::read_var_u64(&mut malformed) {
        Result<u64, CodecError>::Ok(_) => { alloc::box::deallocate(memory); return 14; }
        Result<u64, CodecError>::Error(_) => {}
    }
    alloc::box::deallocate(memory);
    return 0;
}

fn check_address() -> i64 {
    usize raw = alloc::box::allocate(9);
    i64 text[9] = [49,50,55,46,48,46,48,46,49];
    i64 index = 0;
    while (index < 9) { core::bytes::store_u8(raw + index, text[index]); index += 1; }
    Result<Ipv4Address, AddressError> parsed = std::net::address::parse_ipv4(raw, 9);
    match parsed {
        Result<Ipv4Address, AddressError>::Ok(address) => {
            if (std::net::address::ipv4_first(&address) != 127 || std::net::address::ipv4_fourth(&address) != 1) { return 1; }
            String output = std::net::address::ipv4_to_string(&address);
            if (!alloc::string::equals_bytes(&output, raw, 9)) { return 2; }
        }
        Result<Ipv4Address, AddressError>::Error(_) => { return 3; }
    }
    Result<Ipv4Address, AddressError> invalid = std::net::address::ipv4(999, 0, 0, 1);
    match invalid { Result<Ipv4Address, AddressError>::Ok(_) => { return 4; } Result<Ipv4Address, AddressError>::Error(_) => {} }
    alloc::box::deallocate(raw);
    return 0;
}

fn main() -> i64 {
    i64 codec = check_codec(); if (codec != 0) { return 10 + codec; }
    i64 address = check_address(); if (address != 0) { return 30 + address; }
    usize host = alloc::box::allocate(9);
    i64 host_bytes[9] = [49,50,55,46,48,46,48,46,49];
    i64 index = 0; while (index < 9) { core::bytes::store_u8(host + index, host_bytes[index]); index += 1; }
    usize payload = alloc::box::allocate(11);
    i64 payload_bytes[11] = [104, 101, 108, 108, 111, 32, 114, 97, 122, 33];
    index = 0; while (index < 11) { core::bytes::store_u8(payload + index, payload_bytes[index]); index += 1; }
    Result<TcpListener, IoError> listen_result = std::net::try_tcp_listener_bind(0, 8);
    match listen_result {
      Result<TcpListener, IoError>::Error(_) => { return 50; }
      Result<TcpListener, IoError>::Ok(listener) => {
        i64 port = std::net::tcp_listener_local_port(&listener); if (port <= 0) { return 51; }
        Ipv4Address loopback = std::net::address::loopback_ipv4();
        SocketAddress endpoint = SocketAddress { address: loopback, port: port };
        Result<TcpStream, IoError> client_result = std::net::typed::try_tcp_connect(&endpoint);
        match client_result {
          Result<TcpStream, IoError>::Error(_) => { return 52; }
          Result<TcpStream, IoError>::Ok(client_raw) => {
            Result<TcpStream, IoError> server_result = std::net::try_tcp_listener_accept(&listener);
            match server_result {
              Result<TcpStream, IoError>::Error(_) => { return 53; }
              Result<TcpStream, IoError>::Ok(server_raw) => {
                Result<BufferedTcpStream, StreamError> client_buffered = std::net::buffered::from_stream(move client_raw, 64);
                match client_buffered {
                  Result<BufferedTcpStream, StreamError>::Error(_) => { return 54; }
                  Result<BufferedTcpStream, StreamError>::Ok(client) => {
                    Result<BufferedTcpStream, StreamError> server_buffered = std::net::buffered::from_stream(move server_raw, 64);
                    match server_buffered {
                      Result<BufferedTcpStream, StreamError>::Error(_) => { return 55; }
                      Result<BufferedTcpStream, StreamError>::Ok(server) => {
                        Result<bool, StreamError> sent = std::net::framed::write_frame(&mut client, payload, 11, 1024);
                        match sent { Result<bool, StreamError>::Error(_) => { return 56; } Result<bool, StreamError>::Ok(_) => {} }
                        ByteBuffer received = byte_buffer_with_capacity(32);
                        Result<i64, StreamError> frame = std::net::framed::read_frame(&mut server, &mut received, 1024);
                        match frame {
                          Result<i64, StreamError>::Error(_) => { return 57; }
                          Result<i64, StreamError>::Ok(length) => {
                            if (length != 11 || !core::bytes::equal(byte_buffer_view(&received), BytesView { data: payload, length: 11 })) { return 58; }
                          }
                        }
                        Result<bool, StreamError> reply = std::net::framed::write_frame(&mut server, payload, 11, 1024);
                        match reply { Result<bool, StreamError>::Error(_) => { return 59; } Result<bool, StreamError>::Ok(_) => {} }
                        ByteBuffer echoed = byte_buffer_new();
                        Result<i64, StreamError> echoed_frame = std::net::framed::read_frame(&mut client, &mut echoed, 1024);
                        match echoed_frame {
                          Result<i64, StreamError>::Error(_) => { return 60; }
                          Result<i64, StreamError>::Ok(length) => { if (length != 11 || !core::bytes::equal(byte_buffer_view(&echoed), BytesView { data: payload, length: 11 })) { return 61; } }
                        }
                        // Oversized frame rejects before touching the transport.
                        Result<bool, StreamError> oversized = std::net::framed::write_frame(&mut client, payload, 11, 10);
                        match oversized { Result<bool, StreamError>::Ok(_) => { return 62; } Result<bool, StreamError>::Error(_) => {} }
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
    alloc::box::deallocate(host); alloc::box::deallocate(payload); return 0;
}
]=])
execute_process(COMMAND "${RAZ_EXE}" build "${WORK_ROOT}" --target test-host --force
  RESULT_VARIABLE build_result OUTPUT_VARIABLE build_output ERROR_VARIABLE build_error)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "Network/codec build failed:\n${build_error}\n${build_output}")
endif()
if(WIN32)
  set(runtime_exe "${WORK_ROOT}/target/test-host/debug/passl_network_codec.exe")
else()
  set(runtime_exe "${WORK_ROOT}/target/test-host/debug/passl_network_codec")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env TERM=xterm "${runtime_exe}"
  RESULT_VARIABLE runtime_result OUTPUT_VARIABLE runtime_output ERROR_VARIABLE runtime_error)
if(NOT runtime_result EQUAL 0)
  message(FATAL_ERROR "Network/codec runtime returned ${runtime_result}:\n${runtime_error}\n${runtime_output}")
endif()
