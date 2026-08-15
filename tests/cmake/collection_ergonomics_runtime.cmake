# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED RAZ_EXE OR NOT DEFINED SOURCE_ROOT OR NOT DEFINED WORK_ROOT)
  message(FATAL_ERROR "Collection ergonomics runtime requires RAZ_EXE, SOURCE_ROOT, WORK_ROOT")
endif()
file(REMOVE_RECURSE "${WORK_ROOT}")
file(MAKE_DIRECTORY
  "${WORK_ROOT}/src/core/option" "${WORK_ROOT}/src/core/iter" "${WORK_ROOT}/src/core/hash" "${WORK_ROOT}/src/core/slice"
  "${WORK_ROOT}/src/alloc/vec" "${WORK_ROOT}/src/alloc/deque"
  "${WORK_ROOT}/src/collections/vector" "${WORK_ROOT}/src/collections/deque"
  "${WORK_ROOT}/src/collections/hash_set" "${WORK_ROOT}/src/collections/hash_map")
foreach(source_file
    library/core/option/option.rz library/core/iter/iterator.rz library/core/hash/hash.rz library/core/slice/slice.rz
    library/alloc/vec/vec.rz library/alloc/deque/deque.rz
    library/collections/vector/vector.rz library/collections/deque/deque.rz
    library/collections/hash_set/hash_set.rz library/collections/hash_map/hash_map.rz)
  string(REGEX REPLACE "^library/" "src/" destination "${source_file}")
  configure_file("${SOURCE_ROOT}/${source_file}" "${WORK_ROOT}/${destination}" COPYONLY)
endforeach()
file(WRITE "${WORK_ROOT}/raz.toml" [=[
[package]
name = "collection_ergonomics_runtime_fixture"
version = "1.0.0"
kind = "executable"
entry = "src/main.rz"
]=])
file(WRITE "${WORK_ROOT}/src/main.rz" [=[
import core::option;
import core::hash;
import collections::vector;
import collections::deque;
import collections::hash_set;
import collections::hash_map;
fn main() -> i64 {
    Vector<i64> vector = Vector<i64>::new(); vector.push(1); vector.push(2);
    Option<i64> popped = vector.pop();
    match popped { Option<i64>::None => { return 1; } Option<i64>::Some(value) => { if (value != 2) { return 2; } } }
    if (vector.len() != 1) { return 3; }
    i64 one = 1; if (!vector.contains(&one) || vector.position(&one) != 0 || vector.count(&one) != 1) { return 14; }
    vector.push(1); if (vector.count(&one) != 2) { return 16; }
    Option<i64> first_removed = vector.remove_first(&one);
    match first_removed { Option<i64>::None => { return 17; } Option<i64>::Some(value) => { if (value != 1) { return 18; } } }
    vector.push(3); Option<i64> removed = vector.remove(0);
    match removed { Option<i64>::None => { return 4; } Option<i64>::Some(value) => { if (value != 1) { return 5; } } }

    Deque<i64> deque = Deque<i64>::new(); deque.push_front(4); deque.push_back(5); deque.push_back(5); deque.push_back(6);
    i64 five = 5; if (!deque.contains(&five) || deque.position(&five) != 1 || deque.count(&five) != 2) { return 15; }
    Option<i64> middle = deque.remove(2);
    match middle { Option<i64>::None => { return 19; } Option<i64>::Some(value) => { if (value != 5) { return 20; } } }
    Option<i64> first_five = deque.remove_first(&five);
    match first_five { Option<i64>::None => { return 21; } Option<i64>::Some(value) => { if (value != 5) { return 22; } } }
    Option<i64> front = deque.pop_front(); Option<i64> back = deque.pop_back();
    match front { Option<i64>::None => { return 6; } Option<i64>::Some(value) => { if (value != 4) { return 7; } } }
    match back { Option<i64>::None => { return 8; } Option<i64>::Some(value) => { if (value != 6) { return 9; } } }

    HashSet<i64> set = HashSet<i64>::new(); set.insert(7); i64 key = 7;
    Option<i64> taken = set.take(&key); match taken { Option<i64>::None => { return 10; } Option<i64>::Some(value) => { if (value != 7) { return 11; } } }

    HashMap<i64, i64> map = HashMap<i64, i64>::new(); map.insert(9, 10); i64 map_key = 9;
    Option<i64> map_value = map.remove(&map_key);
    match map_value { Option<i64>::None => { return 12; } Option<i64>::Some(value) => { if (value != 10) { return 13; } } }
    return 0;
}
]=])
execute_process(COMMAND "${RAZ_EXE}" build "${WORK_ROOT}" --target test-host --force
  RESULT_VARIABLE build_result OUTPUT_VARIABLE build_output ERROR_VARIABLE build_error)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "Collection ergonomics build failed:\n${build_error}\n${build_output}")
endif()
if(WIN32)
  set(runtime_exe "${WORK_ROOT}/target/test-host/debug/collection_ergonomics_runtime_fixture.exe")
else()
  set(runtime_exe "${WORK_ROOT}/target/test-host/debug/collection_ergonomics_runtime_fixture")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env TERM=xterm "${runtime_exe}"
  RESULT_VARIABLE runtime_result OUTPUT_VARIABLE runtime_output ERROR_VARIABLE runtime_error)
if(NOT runtime_result EQUAL 0)
  message(FATAL_ERROR "Collection ergonomics runtime returned ${runtime_result}:\n${runtime_error}\n${runtime_output}")
endif()
