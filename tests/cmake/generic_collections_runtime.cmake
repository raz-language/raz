# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED RAZC_EXE OR NOT DEFINED SOURCE_ROOT OR NOT DEFINED WORK_ROOT OR NOT DEFINED CASE)
  message(FATAL_ERROR "Collection test requires RAZC_EXE, SOURCE_ROOT, WORK_ROOT, and CASE")
endif()

file(MAKE_DIRECTORY "${WORK_ROOT}")
set(source "")

function(append_raz path)
  file(READ "${SOURCE_ROOT}/${path}" chunk)
  set(source "${source}\n${chunk}\n" PARENT_SCOPE)
endfunction()

append_raz("library/core/iter/iterator.rz")

if(CASE STREQUAL "vector")
  append_raz("library/core/option/option.rz")
  append_raz("library/core/slice/slice.rz")
  append_raz("library/alloc/vec/vec.rz")
  append_raz("library/collections/vector/vector.rz")
  string(APPEND source [=[
fn main() -> i64 {
    Vector<i64> values = Vector<i64>::new();
    values.push(10);
    values.push(20);
    values.push(12);
    i64 total = 0;
    for value in values {
        total += *value;
    }
    return total;
}
]=])
  set(expected "IntoIterator.*Vector<i64>.*into_iter")
elseif(CASE STREQUAL "deque")
  append_raz("library/core/option/option.rz")
  append_raz("library/alloc/deque/deque.rz")
  append_raz("library/collections/deque/deque.rz")
  string(APPEND source [=[
fn main() -> i64 {
    Deque<i64> values = Deque<i64>::new();
    values.push_back(10);
    values.push_front(5);
    i64 total = 0;
    for value in values {
        total += *value;
    }
    return total;
}
]=])
  set(expected "IntoIterator.*Deque<i64>.*into_iter")
elseif(CASE STREQUAL "hash_set")
  append_raz("library/core/option/option.rz")
  append_raz("library/core/hash/hash.rz")
  append_raz("library/collections/hash_set/hash_set.rz")
  string(APPEND source [=[
fn main() -> i64 {
    HashSet<i64> values = HashSet<i64>::new();
    values.insert(10);
    values.insert(20);
    i64 removed_key = 99;
    values.remove(&removed_key);
    i64 total = 0;
    for value in values {
        total += *value;
    }
    return total;
}
]=])
  set(expected "IntoIterator.*HashSet<i64>.*into_iter")
elseif(CASE STREQUAL "hash_map")
  append_raz("library/core/option/option.rz")
  append_raz("library/core/hash/hash.rz")
  append_raz("library/collections/hash_map/hash_map.rz")
  string(APPEND source [=[
fn main() -> i64 {
    HashMap<i64, i64> values = HashMap<i64, i64>::new();
    values.insert(10, 100);
    values.insert(20, 200);
    i64 total = 0;
    for value in values.values() {
        total += *value;
    }
    for entry in values.entries() {
        total += *entry.key();
    }
    return total;
}
]=])
  set(expected "HashMapEntryIter<i64,i64>.*current")
else()
  message(FATAL_ERROR "Unknown Collection case: ${CASE}")
endif()

set(smoke "${WORK_ROOT}/${CASE}.rz")
file(WRITE "${smoke}" "${source}")
execute_process(
  COMMAND "${RAZC_EXE}" --forge-ir "${smoke}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "${CASE} Forge lowering failed:\n${error}\n${output}")
endif()
if(NOT output MATCHES "${expected}")
  message(FATAL_ERROR "${CASE} did not emit expected iterator specialization '${expected}'")
endif()
