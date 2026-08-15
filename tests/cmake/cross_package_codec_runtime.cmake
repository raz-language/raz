# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED RAZ_EXE OR NOT DEFINED SOURCE_ROOT OR NOT DEFINED WORK_ROOT)
  message(FATAL_ERROR "Cross-package codec test requires RAZ_EXE, SOURCE_ROOT, WORK_ROOT")
endif()
file(REMOVE_RECURSE "${WORK_ROOT}")
file(MAKE_DIRECTORY "${WORK_ROOT}/wirelib/src" "${WORK_ROOT}/consumer/src")

set(wire_sources
  library/core/result/result.rz
  library/core/bytes/bytes.rz
  library/alloc/box/box.rz
  library/alloc/string/string.rz
  library/std/encoding/binary/binary.rz
  library/std/net/address/address.rz)
foreach(source_file IN LISTS wire_sources)
  string(REGEX REPLACE "^library/" "src/" destination "${source_file}")
  get_filename_component(destination_dir "${WORK_ROOT}/wirelib/${destination}" DIRECTORY)
  file(MAKE_DIRECTORY "${destination_dir}")
  configure_file("${SOURCE_ROOT}/${source_file}" "${WORK_ROOT}/wirelib/${destination}" COPYONLY)
endforeach()

file(WRITE "${WORK_ROOT}/wirelib/raz.toml" [=[
[package]
name = "wirelib"
version = "1.0.0"
kind = "static-library"
source = "src"
]=])
file(WRITE "${WORK_ROOT}/wirelib/src/wire.rz" [=[
namespace wirelib;
public import core::result;
public import alloc::box;
public import std::encoding::binary;
public import std::net::address;

public fn decode_word(usize data) -> Result<u32, CodecError> {
    BinaryReader input = std::encoding::binary::reader(data, 4);
    return std::encoding::binary::read_u32_be(&mut input);
}

public fn loopback() -> Result<Ipv4Address, AddressError> {
    return std::net::address::ipv4(127, 0, 0, 1);
}
]=])

file(WRITE "${WORK_ROOT}/consumer/raz.toml" [=[
[package]
name = "wireconsumer"
version = "1.0.0"
kind = "executable"
entry = "src/main.rz"
[dependencies]
wirelib = "../wirelib"
]=])
file(WRITE "${WORK_ROOT}/consumer/src/main.rz" [=[
import wirelib;

fn main() -> i64 {
    usize data = allocate(8);
    if (data == 0) { return 1; }
    {
        BinaryWriter output = writer(data, 8);
        match write_u32_be(&mut output, 0x89abcdef as u32) {
            Result<bool, CodecError>::Error(_) => { return 1; }
            Result<bool, CodecError>::Ok(_) => {}
        }
        match decode_word(data) {
            Result<u32, CodecError>::Error(_) => { return 2; }
            Result<u32, CodecError>::Ok(value) => {
                if (value != (0x89abcdef as u32)) { return 3; }
            }
        }
    }
    deallocate(data);
    match loopback() {
        Result<Ipv4Address, AddressError>::Error(_) => { return 4; }
        Result<Ipv4Address, AddressError>::Ok(address) => {
            if (ipv4_first(&address) != 127 || ipv4_fourth(&address) != 1) { return 5; }
        }
    }
    return 0;
}
]=])

execute_process(COMMAND "${RAZ_EXE}" build "${WORK_ROOT}/consumer" --target test-host --force
  RESULT_VARIABLE build_result OUTPUT_VARIABLE build_output ERROR_VARIABLE build_error)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "Cross-package codec build failed:\n${build_error}\n${build_output}")
endif()
if(WIN32)
  set(runtime_exe "${WORK_ROOT}/consumer/target/test-host/debug/wireconsumer.exe")
else()
  set(runtime_exe "${WORK_ROOT}/consumer/target/test-host/debug/wireconsumer")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env TERM=xterm "${runtime_exe}"
  RESULT_VARIABLE runtime_result OUTPUT_VARIABLE runtime_output ERROR_VARIABLE runtime_error)
if(NOT runtime_result EQUAL 0)
  message(FATAL_ERROR "Cross-package codec runtime returned ${runtime_result}:\n${runtime_error}\n${runtime_output}")
endif()
