# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED RAZ_EXE OR NOT DEFINED SOURCE_ROOT OR NOT DEFINED WORK_ROOT)
  message(FATAL_ERROR "Core ergonomics runtime requires RAZ_EXE, SOURCE_ROOT, WORK_ROOT")
endif()
file(REMOVE_RECURSE "${WORK_ROOT}")
file(MAKE_DIRECTORY "${WORK_ROOT}/src/core/bytes" "${WORK_ROOT}/src/alloc/box" "${WORK_ROOT}/src/alloc/string"
  "${WORK_ROOT}/src/std/path/buf" "${WORK_ROOT}/src/std/path" "${WORK_ROOT}/src/std/random" "${WORK_ROOT}/src/std/time")
foreach(source_file
    library/core/bytes/bytes.rz library/alloc/box/box.rz library/alloc/string/string.rz
    library/std/path/path.rz library/std/path/buf/buf.rz library/std/random/random.rz library/std/time/time.rz)
  string(REGEX REPLACE "^library/" "src/" destination "${source_file}")
  configure_file("${SOURCE_ROOT}/${source_file}" "${WORK_ROOT}/${destination}" COPYONLY)
endforeach()
file(WRITE "${WORK_ROOT}/raz.toml" [=[
[package]
name = "passk_core_ergonomics"
version = "1.0.0"
kind = "executable"
entry = "src/main.rz"
]=])
file(WRITE "${WORK_ROOT}/src/main.rz" [=[
import core::bytes;
import alloc::box;
import alloc::string;
import std::path;
import std::path::buf;
import std::random;
import std::time;
fn bytes4(i64 a, i64 b, i64 c, i64 d) -> usize {
    usize pointer = alloc::box::allocate(4);
    core::bytes::store_u8(pointer, a); core::bytes::store_u8(pointer + 1, b);
    core::bytes::store_u8(pointer + 2, c); core::bytes::store_u8(pointer + 3, d);
    return pointer;
}
fn main() -> i64 {
    usize base = bytes4(97,98,99,100);
    String text = alloc::string::from_bytes(base, 4);
    usize alias = alloc::string::data_ptr(&text) + 1;
    if (!alloc::string::append_bytes(&mut text, alias, 2) || alloc::string::len(&text) != 6) { return 1; }
    usize alias2 = alloc::string::data_ptr(&text) + 3;
    if (!alloc::string::insert_bytes(&mut text, 1, alias2, 2) || alloc::string::len(&text) != 8) { return 2; }
    if (!alloc::string::remove_range(&mut text, 1, 2) || alloc::string::len(&text) != 6) { return 3; }
    if (!alloc::string::is_valid_utf8(&text)) { return 4; }
    usize bad = bytes4(240,40,140,188); if (alloc::string::valid_utf8_bytes(bad,4)) { return 5; }
    i64 popped = 0; if (!alloc::string::pop_byte(&mut text, &mut popped) || popped != 99) { return 6; }

    usize raw_path = alloc::box::allocate(16);
    i64 path_bytes[16] = [102,111,111,92,46,92,98,97,114,92,46,46,92,113,117,120];
    i64 index = 0; while (index < 16) { core::bytes::store_u8(raw_path + index, path_bytes[index]); index += 1; }
    PathBuf path = PathBuf::from_bytes(raw_path, 16); if (!path.normalize()) { return 7; }
    if (path.len() != 7 || std::path::byte_at(path.as_view(), 3) != 47 || std::path::byte_at(path.as_view(), 4) != 113) { return 8; }
    usize part = alloc::box::allocate(7); i64 part_bytes[7] = [98,97,122,46,116,120,116];
    index = 0; while (index < 7) { core::bytes::store_u8(part + index, part_bytes[index]); index += 1; }
    if (!path.push(PathView { data: part, length: 7 })) { return 9; }
    PathView filename = path.filename(); PathView extension = path.extension(); PathView stem = path.stem();
    if (filename.length != 7 || extension.length != 3 || stem.length != 3 || std::path::byte_at(extension,0) != 116) { return 10; }
    usize log_ext = alloc::box::allocate(3); core::bytes::store_u8(log_ext,108); core::bytes::store_u8(log_ext+1,111); core::bytes::store_u8(log_ext+2,103);
    if (!path.set_extension(PathView { data: log_ext, length: 3 })) { return 19; }
    extension = path.extension(); if (extension.length != 3 || std::path::byte_at(extension,0) != 108) { return 16; }
    if (!path.pop() || path.len() != 7) { return 17; }

    usize entropy = alloc::box::allocate(32); if (!std::random::fill(entropy, 32)) { return 18; }
    Rng first = Rng::seeded(1234); Rng second = Rng::seeded(1234);
    if (first.next_u64() != second.next_u64()) { return 19; }
    i64 ranged = first.range_i64(10,20); if (ranged < 10 || ranged >= 20) { return 16; }
    if (!first.seed_from_system()) { return 17; }

    Duration delay = std::time::milliseconds(2); if (std::time::as_nanos(&delay) != 2000000) { return 18; }
    Instant start = std::time::now(); std::time::sleep(&delay); Duration elapsed = std::time::elapsed(&start);
    if (std::time::as_nanos(&elapsed) < 0) { return 19; }

    alloc::box::deallocate(base); alloc::box::deallocate(bad); alloc::box::deallocate(raw_path);
    alloc::box::deallocate(part); alloc::box::deallocate(log_ext); alloc::box::deallocate(entropy); return 0;
}
]=])
execute_process(COMMAND "${RAZ_EXE}" build "${WORK_ROOT}" --target test-host --force
  RESULT_VARIABLE build_result OUTPUT_VARIABLE build_output ERROR_VARIABLE build_error)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "Core ergonomics build failed:\n${build_error}\n${build_output}")
endif()
if(WIN32)
  set(runtime_exe "${WORK_ROOT}/target/test-host/debug/passk_core_ergonomics.exe")
else()
  set(runtime_exe "${WORK_ROOT}/target/test-host/debug/passk_core_ergonomics")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env TERM=xterm "${runtime_exe}"
  RESULT_VARIABLE runtime_result OUTPUT_VARIABLE runtime_output ERROR_VARIABLE runtime_error)
if(NOT runtime_result EQUAL 0)
  message(FATAL_ERROR "Core ergonomics runtime returned ${runtime_result}:\n${runtime_error}\n${runtime_output}")
endif()
