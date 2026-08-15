# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED RAZ_EXE OR NOT DEFINED SOURCE_ROOT OR NOT DEFINED WORK_ROOT)
  message(FATAL_ERROR "Core stdlib runtime requires RAZ_EXE, SOURCE_ROOT, WORK_ROOT")
endif()
include("${SOURCE_ROOT}/tests/cmake/copy_stdlib_closure.cmake")

file(REMOVE_RECURSE "${WORK_ROOT}")
file(MAKE_DIRECTORY "${WORK_ROOT}/src")
file(WRITE "${WORK_ROOT}/raz.toml" [=[
[package]
name = "core_stdlib_runtime_fixture"
version = "1.0.0"
kind = "executable"
entry = "src/main.rz"
]=])
file(WRITE "${WORK_ROOT}/src/main.rz" [=[
import core::option;
import core::result;
import core::iter::iterator;
import core::slice;
import core::bytes;
import core::hash;
import alloc::vec;
import alloc::deque;
import alloc::string;
import alloc::string::hash;
import collections::vector;
import collections::deque;
import collections::hash_map;
import collections::hash_set;
import std::io;
import std::io::buffer;
import std::fmt;
import std::random;

fn make_string(i64 a, i64 b, i64 c) -> String {
    usize data = raz_rt_alloc(3);
    raz_rt_store_u8(data, a);
    raz_rt_store_u8(data + 1, b);
    raz_rt_store_u8(data + 2, c);
    String out = alloc::string::from_bytes(data, 3);
    raz_rt_dealloc(data);
    return out;
}

fn main() -> i64 {
    Option<i64> option = Option<i64>::Some(7);
    Option<i64> taken = option.take();
    if (!option.is_none()) { return 1; }
    match taken { Option<i64>::Some(v) => { if (v != 7) { return 2; } } Option<i64>::None => { return 3; } }
    Option<i64> old = option.replace(11);
    if (!old.is_none() || !option.is_some()) { return 4; }

    Result<i64, i64> ok = Result<i64, i64>::Ok(9);
    if (!ok.is_ok()) { return 5; }
    Vector<i64> left = Vector<i64>::with_capacity(2);
    left.push(1); left.push(3); left.insert(1, 2);
    if (left.len() != 3) { return 7; }
    unsafe { if (*left.get_ptr(1) != 2) { return 8; } }
    Option<i64> swapped = left.swap_remove(1);
    match swapped { Option<i64>::Some(v) => { if (v != 2) { return 9; } } Option<i64>::None => { return 10; } }
    Vector<i64> right = Vector<i64>::new(); right.push(4); right.push(5);
    if (!left.append(&mut right) || right.len() != 0 || left.len() != 4) { return 11; }
    Slice<i64> view = left.as_slice();
    if (view.len() != 4 || !view.valid()) { return 12; }
    Slice<i64> middle = view.subslice(1, 2);
    unsafe { if (*middle.first_ptr() != 3) { return 13; } }
    SliceIter<i64> iterator = view.iter();
    i64 sum = 0;
    while (iterator.next()) { sum += *iterator.current(); }
    if (sum != 13) { return 14; }
    if (!left.shrink_to_fit() || left.capacity() != left.len()) { return 15; }

    Deque<i64> deque = Deque<i64>::with_capacity(32); deque.push_back(1); deque.push_front(2);
    if (!deque.shrink_to_fit() || deque.capacity() != 2) { return 16; }

    HashMap<i64, i64> map = HashMap<i64, i64>::new();
    if (!map.reserve(100)) { return 17; }
    i64 reserved = map.capacity();
    i64 i = 0;
    while (i < 100) { if (!map.insert(i, i * 3)) { return 18; } i += 1; }
    if (map.capacity() != reserved) { return 19; }
    i64 key = 50;
    unsafe { if (*map.get_ptr(&key) != 150) { return 20; } }
    i = 0; while (i < 70) { i64 k = i; map.remove(&k); i += 1; }
    i = 100; while (i < 150) { if (!map.insert(i, i)) { return 21; } i += 1; }

    HashSet<u64> set = HashSet<u64>::new();
    u64 uk = 99; if (!set.insert(uk) || !set.contains(&uk)) { return 22; }

    HashMap<String, i64> strings = HashMap<String, i64>::new();
    String abc = make_string(97, 98, 99); String abc_lookup = make_string(97, 98, 99);
    if (!strings.insert(move abc, 44)) { return 23; }
    unsafe { if (*strings.get_ptr(&abc_lookup) != 44) { return 24; } }

    i64 minimum_value = -9223372036854775807; minimum_value -= 1;
    String minimum = std::fmt::format_i64(minimum_value);
    if (alloc::string::len(&minimum) != 20 || alloc::string::byte_at(&minimum, 0) != 45) { return 25; }
    u64 maximum_value = (0 as u64) - 1;
    String hex = std::fmt::format_hex_u64(maximum_value, false);
    if (alloc::string::len(&hex) != 16 || alloc::string::byte_at(&hex, 0) != 102) { return 26; }

    usize bytes = raz_rt_alloc(1024);
    i = 0; while (i < 1024) { raz_rt_store_u8(bytes + i, i & 255); i += 1; }
    if (core::bytes::find_byte(BytesView { data: bytes, length: 1024 }, 250) != 250) { return 27; }
    if (core::bytes::rfind_byte(BytesView { data: bytes, length: 1024 }, 250) != 1018) { return 28; }

    ByteBuffer buffer = ByteBuffer::with_capacity(64);
    if (!buffer.append(bytes, 48) || !buffer.consume(40)) { return 29; }
    i64 before = buffer.capacity();
    if (!buffer.append(bytes + 48, 32)) { return 30; }
    if (buffer.capacity() != before || buffer.readable() != 40) { return 31; }
    buffer.shrink_to_fit();
    if (buffer.capacity() != buffer.readable()) { return 32; }

    Rng rng = rng_seeded(123);
    i = 0; while (i < 1000) { i64 x = rng_range_i64(&mut rng, -100, 100); if (x < -100 || x >= 100) { return 33; } i += 1; }

    raz_rt_dealloc(bytes);
    return 0;
}
]=])

raz_copy_stdlib_closure()

execute_process(COMMAND "${RAZ_EXE}" build "${WORK_ROOT}" --target test-host --profile debug --force
  RESULT_VARIABLE build_result OUTPUT_VARIABLE build_output ERROR_VARIABLE build_error)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "Core stdlib build failed:\n${build_error}\n${build_output}")
endif()
if(WIN32)
  set(runtime_exe "${WORK_ROOT}/target/test-host/debug/core_stdlib_runtime_fixture.exe")
else()
  set(runtime_exe "${WORK_ROOT}/target/test-host/debug/core_stdlib_runtime_fixture")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env TERM=xterm "${runtime_exe}"
  RESULT_VARIABLE runtime_result OUTPUT_VARIABLE runtime_output ERROR_VARIABLE runtime_error)
if(NOT runtime_result EQUAL 0)
  message(FATAL_ERROR "Core stdlib runtime returned ${runtime_result}:\n${runtime_error}\n${runtime_output}")
endif()
