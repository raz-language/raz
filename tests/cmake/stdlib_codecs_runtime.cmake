# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED RAZ_EXE OR NOT DEFINED SOURCE_ROOT OR NOT DEFINED WORK_ROOT)
  message(FATAL_ERROR "Stdlib codec runtime requires RAZ_EXE, SOURCE_ROOT, WORK_ROOT")
endif()
file(REMOVE_RECURSE "${WORK_ROOT}")
file(MAKE_DIRECTORY "${WORK_ROOT}/src")
set(codec_sources
  library/core/result/result.rz
  library/core/bytes/bytes.rz
  library/core/ascii/ascii.rz
  library/alloc/box/box.rz
  library/std/encoding/hex/hex.rz
  library/std/encoding/base64/base64.rz
  library/std/encoding/checksum/crc32.rz
  library/std/net/url/url.rz)
foreach(source_file IN LISTS codec_sources)
  string(REGEX REPLACE "^library/" "src/" destination "${source_file}")
  get_filename_component(destination_dir "${WORK_ROOT}/${destination}" DIRECTORY)
  file(MAKE_DIRECTORY "${destination_dir}")
  configure_file("${SOURCE_ROOT}/${source_file}" "${WORK_ROOT}/${destination}" COPYONLY)
endforeach()
file(WRITE "${WORK_ROOT}/raz.toml" [=[
[package]
name = "passm_stdlib_codecs"
version = "1.0.0"
kind = "executable"
entry = "src/main.rz"
]=])
file(WRITE "${WORK_ROOT}/src/main.rz" [=[
import core::result;
import core::bytes;
import core::ascii;
import alloc::box;
import std::encoding::hex;
import std::encoding::base64;
import std::encoding::checksum;
import std::net::url;

fn main() -> i64 {
    usize source = alloc::box::allocate(64);
    usize encoded = alloc::box::allocate(128);
    usize decoded = alloc::box::allocate(128);
    if (source == 0 || encoded == 0 || decoded == 0) { return 1; }

    i64 hello[5] = [104, 101, 108, 108, 111];
    i64 index = 0;
    while (index < 5) { core::bytes::store_u8(source + index, hello[index]); index += 1; }

    match std::encoding::hex::encode_lower(source, 5, encoded, 128) {
        Result<i64, HexError>::Error(_) => { return 2; }
        Result<i64, HexError>::Ok(length) => {
            if (length != 10 || core::bytes::load_u8(encoded) != 54 || core::bytes::load_u8(encoded + 9) != 102) { return 3; }
        }
    }
    match std::encoding::hex::decode(encoded, 10, decoded, 128) {
        Result<i64, HexError>::Error(_) => { return 4; }
        Result<i64, HexError>::Ok(length) => {
            if (length != 5 || !core::bytes::equal(BytesView { data: source, length: 5 }, BytesView { data: decoded, length: 5 })) { return 5; }
        }
    }

    match std::encoding::base64::encode(source, 5, encoded, 128) {
        Result<i64, Base64Error>::Error(_) => { return 6; }
        Result<i64, Base64Error>::Ok(length) => {
            if (length != 8 || core::bytes::load_u8(encoded) != 97 || core::bytes::load_u8(encoded + 7) != 61) { return 7; }
        }
    }
    match std::encoding::base64::decode(encoded, 8, decoded, 128) {
        Result<i64, Base64Error>::Error(_) => { return 8; }
        Result<i64, Base64Error>::Ok(length) => {
            if (length != 5 || !core::bytes::equal(BytesView { data: source, length: 5 }, BytesView { data: decoded, length: 5 })) { return 9; }
        }
    }

    // Deterministic round-trip stress across every short payload length. This
    // catches tail/padding bugs without making the test nondeterministic.
    i64 fuzz_length = 0;
    while (fuzz_length <= 64) {
        index = 0;
        while (index < fuzz_length) {
            core::bytes::store_u8(source + index, (index * 37 + fuzz_length * 11) & 255);
            index += 1;
        }

        match std::encoding::hex::encode_lower(source, fuzz_length, encoded, 128) {
            Result<i64, HexError>::Error(_) => { return 20; }
            Result<i64, HexError>::Ok(hex_length) => {
                match std::encoding::hex::decode(encoded, hex_length, decoded, 128) {
                    Result<i64, HexError>::Error(_) => { return 21; }
                    Result<i64, HexError>::Ok(decoded_length) => {
                        if (decoded_length != fuzz_length || !core::bytes::equal(
                            BytesView { data: source, length: fuzz_length },
                            BytesView { data: decoded, length: decoded_length }
                        )) { return 22; }
                    }
                }
            }
        }

        match std::encoding::base64::encode(source, fuzz_length, encoded, 128) {
            Result<i64, Base64Error>::Error(_) => { return 23; }
            Result<i64, Base64Error>::Ok(encoded_count) => {
                match std::encoding::base64::decode(encoded, encoded_count, decoded, 128) {
                    Result<i64, Base64Error>::Error(_) => { return 24; }
                    Result<i64, Base64Error>::Ok(decoded_length) => {
                        if (decoded_length != fuzz_length || !core::bytes::equal(
                            BytesView { data: source, length: fuzz_length },
                            BytesView { data: decoded, length: decoded_length }
                        )) { return 25; }
                    }
                }
            }
        }

        match std::encoding::base64::encode_url(source, fuzz_length, encoded, 128) {
            Result<i64, Base64Error>::Error(_) => { return 26; }
            Result<i64, Base64Error>::Ok(encoded_count) => {
                match std::encoding::base64::decode_url(encoded, encoded_count, decoded, 128) {
                    Result<i64, Base64Error>::Error(_) => { return 27; }
                    Result<i64, Base64Error>::Ok(decoded_length) => {
                        if (decoded_length != fuzz_length || !core::bytes::equal(
                            BytesView { data: source, length: fuzz_length },
                            BytesView { data: decoded, length: decoded_length }
                        )) { return 28; }
                    }
                }
            }
        }
        fuzz_length += 1;
    }

    // Invalid padding placement and non-zero discarded bits must be rejected.
    core::bytes::store_u8(encoded, 90);      // Z
    core::bytes::store_u8(encoded + 1, 71);  // G
    core::bytes::store_u8(encoded + 2, 61);  // =
    core::bytes::store_u8(encoded + 3, 90);  // Z
    match std::encoding::base64::decode(encoded, 4, decoded, 128) {
        Result<i64, Base64Error>::Ok(_) => { return 29; }
        Result<i64, Base64Error>::Error(_) => {}
    }
    core::bytes::store_u8(encoded, 90);      // Z
    core::bytes::store_u8(encoded + 1, 104); // h: low padding bits are nonzero
    core::bytes::store_u8(encoded + 2, 61);
    core::bytes::store_u8(encoded + 3, 61);
    match std::encoding::base64::decode(encoded, 4, decoded, 128) {
        Result<i64, Base64Error>::Ok(_) => { return 30; }
        Result<i64, Base64Error>::Error(_) => {}
    }

    index = 0;
    while (index < 5) { core::bytes::store_u8(source + index, hello[index]); index += 1; }
    u32 checksum = std::encoding::checksum::crc32(source, 5);
    if (checksum != (0x3610a686 as u32)) { return 10; }

    i64 url_bytes[44] = [104, 116, 116, 112, 115, 58, 47, 47, 101, 120, 97, 109, 112, 108, 101, 46, 99, 111, 109, 58, 56, 52, 52, 51, 47, 97, 112, 105, 47, 118, 49, 63, 113, 61, 114, 97, 122, 35, 116, 111, 112, 33, 33, 33];
    // Parse only through "top"; the trailing sentinel bytes prove the supplied length is honored.
    index = 0;
    while (index < 44) { core::bytes::store_u8(source + index, url_bytes[index]); index += 1; }
    match std::net::url::parse(source, 41) {
        Result<UrlView, UrlError>::Error(_) => { return 11; }
        Result<UrlView, UrlError>::Ok(value) => {
            if (!std::net::url::is_https(&value)) { return 12; }
            if (value.host.length != 11 || value.port != 8443 || !value.explicit_port) { return 13; }
            if (value.path.length != 7 || value.query.length != 5 || value.fragment.length != 3) { return 14; }
        }
    }

    alloc::box::deallocate(source);
    alloc::box::deallocate(encoded);
    alloc::box::deallocate(decoded);
    return 0;
}
]=])
execute_process(COMMAND "${RAZ_EXE}" build "${WORK_ROOT}" --target test-host --force
  RESULT_VARIABLE build_result OUTPUT_VARIABLE build_output ERROR_VARIABLE build_error)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "Stdlib codec build failed:\n${build_error}\n${build_output}")
endif()
if(WIN32)
  set(runtime_exe "${WORK_ROOT}/target/test-host/debug/passm_stdlib_codecs.exe")
else()
  set(runtime_exe "${WORK_ROOT}/target/test-host/debug/passm_stdlib_codecs")
endif()
execute_process(COMMAND "${runtime_exe}"
  RESULT_VARIABLE runtime_result OUTPUT_VARIABLE runtime_output ERROR_VARIABLE runtime_error)
if(NOT runtime_result EQUAL 0)
  message(FATAL_ERROR "Stdlib codec runtime returned ${runtime_result}:\n${runtime_error}\n${runtime_output}")
endif()
