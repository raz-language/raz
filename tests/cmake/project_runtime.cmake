# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED RAZ_EXE OR NOT DEFINED SOURCE_ROOT OR NOT DEFINED WORK_ROOT)
  message(FATAL_ERROR "Project runtime test requires RAZ_EXE, SOURCE_ROOT, and WORK_ROOT")
endif()

file(REMOVE_RECURSE "${WORK_ROOT}")
file(MAKE_DIRECTORY
  "${WORK_ROOT}/src/core/iter"
  "${WORK_ROOT}/src/core/hash"
  "${WORK_ROOT}/src/alloc/vec"
  "${WORK_ROOT}/src/alloc/deque"
  "${WORK_ROOT}/src/collections/vector"
  "${WORK_ROOT}/src/collections/deque"
  "${WORK_ROOT}/src/collections/hash_set"
  "${WORK_ROOT}/src/collections/hash_map")

foreach(source_file
    library/core/iter/iterator.rz
    library/core/hash/hash.rz
    library/alloc/vec/vec.rz
    library/alloc/deque/deque.rz
    library/collections/vector/vector.rz
    library/collections/deque/deque.rz
    library/collections/hash_set/hash_set.rz
    library/collections/hash_map/hash_map.rz)
  string(REGEX REPLACE "^library/" "src/" destination "${source_file}")
  configure_file("${SOURCE_ROOT}/${source_file}" "${WORK_ROOT}/${destination}" COPYONLY)
endforeach()

file(WRITE "${WORK_ROOT}/raz.toml" [=[
[package]
name = "passh_all_collections"
version = "1.0.0"
kind = "executable"
entry = "src/main.rz"
]=])

file(WRITE "${WORK_ROOT}/src/main.rz" [=[
import collections::vector;
import collections::deque;
import collections::hash_set;
import collections::hash_map;

fn main() -> i64 {
    Vector<i64> vector = Vector<i64>::new();
    if (!vector.push(10)) { return 1; }
    if (!vector.push(20)) { return 2; }
    if (!vector.push(12)) { return 3; }
    i64 vector_sum = 0;
    for value in vector { vector_sum += *value; }
    if (vector_sum != 42) { return 4; }

    Deque<i64> deque = Deque<i64>::new();
    if (!deque.push_back(10)) { return 5; }
    if (!deque.push_front(5)) { return 6; }
    if (!deque.push_back(7)) { return 7; }
    i64 deque_sum = 0;
    for value in deque { deque_sum += *value; }
    if (deque_sum != 22) { return 8; }
    i64 front = 0;
    if (!deque.try_pop_front(&mut front)) { return 9; }
    if (front != 5) { return 10; }
    if (deque.len() != 2) { return 11; }

    HashSet<i64> set = HashSet<i64>::new();
    if (!set.insert(10)) { return 12; }
    if (!set.insert(20)) { return 13; }
    if (set.insert(20)) { return 14; }
    i64 set_sum = 0;
    for value in set { set_sum += *value; }
    if (set_sum != 30) { return 15; }
    i64 key10 = 10;
    if (!set.contains(&key10)) { return 16; }
    if (!set.remove(&key10)) { return 17; }
    if (set.contains(&key10)) { return 18; }

    HashMap<i64, i64> map = HashMap<i64, i64>::new();
    if (!map.insert(1, 10)) { return 19; }
    if (!map.insert(2, 20)) { return 20; }
    if (!map.insert(1, 15)) { return 21; }
    i64 value_sum = 0;
    for value in map.values() { value_sum += *value; }
    if (value_sum != 35) { return 22; }
    i64 key_sum = 0;
    for entry in map.entries() { key_sum += *entry.key(); }
    if (key_sum != 3) { return 23; }
    i64 key2 = 2;
    i64 removed_value = 0;
    if (!map.try_remove(&key2, &mut removed_value)) { return 24; }
    if (removed_value != 20) { return 25; }
    if (map.len() != 1) { return 26; }
    return 0;
}
]=])

execute_process(
  COMMAND "${RAZ_EXE}" build "${WORK_ROOT}" --target test-host --force
  RESULT_VARIABLE build_result
  OUTPUT_VARIABLE build_output
  ERROR_VARIABLE build_error)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "Separate-module collection project failed to build:\n${build_error}\n${build_output}")
endif()

if(WIN32)
  set(runtime_exe "${WORK_ROOT}/target/test-host/debug/passh_all_collections.exe")
else()
  set(runtime_exe "${WORK_ROOT}/target/test-host/debug/passh_all_collections")
endif()
if(NOT EXISTS "${runtime_exe}")
  message(FATAL_ERROR "Runtime executable was not produced: ${runtime_exe}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env TERM=xterm "${runtime_exe}"
  RESULT_VARIABLE runtime_result
  OUTPUT_VARIABLE runtime_output
  ERROR_VARIABLE runtime_error)
if(NOT runtime_result EQUAL 0)
  message(FATAL_ERROR
    "Collection runtime returned ${runtime_result}:\n${runtime_error}\n${runtime_output}")
endif()
