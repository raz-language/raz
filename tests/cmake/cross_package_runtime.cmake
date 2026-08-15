# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED RAZ_EXE OR NOT DEFINED SOURCE_ROOT OR NOT DEFINED WORK_ROOT)
  message(FATAL_ERROR "Cross-package runtime test requires RAZ_EXE, SOURCE_ROOT, and WORK_ROOT")
endif()

file(REMOVE_RECURSE "${WORK_ROOT}")
set(library_root "${WORK_ROOT}/collections-lib")
set(app_root "${WORK_ROOT}/consumer")
file(MAKE_DIRECTORY
  "${library_root}/src/core/iter"
  "${library_root}/src/core/hash"
  "${library_root}/src/alloc/vec"
  "${library_root}/src/alloc/deque"
  "${library_root}/src/collections/vector"
  "${library_root}/src/collections/deque"
  "${library_root}/src/collections/hash_set"
  "${library_root}/src/collections/hash_map"
  "${app_root}/src")

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
  configure_file("${SOURCE_ROOT}/${source_file}" "${library_root}/${destination}" COPYONLY)
endforeach()

file(WRITE "${library_root}/raz.toml" [=[
[package]
name = "passh_collections_lib"
version = "1.0.0"
kind = "static-library"
entry = "src/collections/vector/vector.rz"
]=])

file(WRITE "${app_root}/raz.toml" [=[
[package]
name = "passh_collection_consumer"
version = "1.0.0"
kind = "executable"
entry = "src/main.rz"

[dependencies]
collections = "../collections-lib"
]=])

file(WRITE "${app_root}/src/main.rz" [=[
import collections::collections::vector;
import collections::collections::hash_map;

fn main() -> i64 {
    Vector<i64> values = Vector<i64>::new();
    values.push(20);
    values.push(22);
    i64 total = 0;
    for value in values { total += *value; }

    HashMap<i64, i64> map = HashMap<i64, i64>::new();
    map.insert(1, total);
    i64 map_total = 0;
    for value in map.values() { map_total += *value; }
    return map_total - 42;
}
]=])

execute_process(
  COMMAND "${RAZ_EXE}" build "${app_root}" --target test-host --force
  RESULT_VARIABLE build_result
  OUTPUT_VARIABLE build_output
  ERROR_VARIABLE build_error)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "Cross-package generic collection build failed:\n${build_error}\n${build_output}")
endif()

if(WIN32)
  set(runtime_exe "${app_root}/target/test-host/debug/passh_collection_consumer.exe")
else()
  set(runtime_exe "${app_root}/target/test-host/debug/passh_collection_consumer")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env TERM=xterm "${runtime_exe}"
  RESULT_VARIABLE runtime_result
  OUTPUT_VARIABLE runtime_output
  ERROR_VARIABLE runtime_error)
if(NOT runtime_result EQUAL 0)
  message(FATAL_ERROR "Cross-package collection runtime returned ${runtime_result}:\n${runtime_error}\n${runtime_output}")
endif()
