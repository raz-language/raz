# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED RAZ_EXE OR NOT DEFINED SOURCE_ROOT OR NOT DEFINED WORK_ROOT)
  message(FATAL_ERROR "Compression runtime requires RAZ_EXE, SOURCE_ROOT, WORK_ROOT")
endif()
include("${SOURCE_ROOT}/tests/cmake/copy_stdlib_closure.cmake")
file(REMOVE_RECURSE "${WORK_ROOT}")
file(MAKE_DIRECTORY "${WORK_ROOT}/src")
file(WRITE "${WORK_ROOT}/raz.toml" [=[
[package]
name = "compression_runtime_fixture"
version = "1.0.0"
kind = "executable"
entry = "src/main.rz"
]=])
file(WRITE "${WORK_ROOT}/src/main.rz" [=[
import alloc::string;
import std::compress::lz4;

fn load_u8(usize address) -> i64 {
    unsafe { return (*(address as u8*const))as i64; }
}

fn round_trip(String& input, Encoder&mut encoder) -> i64 {
    i64 input_length = alloc::string::len(input);
    i64 bound = std::compress::lz4::compress_bound(input_length);
    if (bound <= 0) { return 10; }
    String packed = alloc::string::with_capacity(bound);
    i64 packed_length = std::compress::lz4::compress(
        encoder,
        alloc::string::data_ptr(input),
        input_length,
        alloc::string::data_ptr(&packed),
        bound
    );
    if (packed_length <= 0 || packed_length > bound) { return 11; }
    String decoded = alloc::string::with_capacity(input_length);
    i64 decoded_length = std::compress::lz4::decompress(
        alloc::string::data_ptr(&packed),
        packed_length,
        alloc::string::data_ptr(&decoded),
        input_length
    );
    if (decoded_length != input_length) { return 12; }
    i64 i = 0;
    while (i < input_length) {
        if (load_u8(alloc::string::data_ptr(&decoded) + i) != load_u8(alloc::string::data_ptr(input) + i)) {
            return 13;
        }
        i += 1;
    }
    return packed_length;
}

fn main() -> i64 {
    Encoder encoder = std::compress::lz4::create_encoder();
    if (!std::compress::lz4::encoder_valid(&encoder)) { return 1; }

    String repeated = alloc::string::with_capacity(65536);
    i64 i = 0;
    while (i < 65536) {
        alloc::string::push_byte(&mut repeated, 65 + (i & 3));
        i += 1;
    }
    i64 repeated_size = round_trip(&repeated, &mut encoder);
    if (repeated_size <= 0 || repeated_size >= 4096) { return 2; }

    String mixed = alloc::string::with_capacity(8192);
    i = 0;
    while (i < 8192) {
        alloc::string::push_byte(&mut mixed, ((i * 131 + (i >> 3) * 17) & 255));
        i += 1;
    }
    if (round_trip(&mixed, &mut encoder) <= 0) { return 3; }

    return 0;
}
]=])
raz_copy_stdlib_closure()
execute_process(COMMAND "${RAZ_EXE}" run "${WORK_ROOT}" --profile debug --color never
  RESULT_VARIABLE run_result OUTPUT_VARIABLE run_output ERROR_VARIABLE run_error)
if(NOT run_result EQUAL 0)
  message(FATAL_ERROR "Compression runtime failed (${run_result}):\n${run_error}\n${run_output}")
endif()
