# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED RAZ_EXE OR NOT DEFINED SOURCE_ROOT OR NOT DEFINED WORK_ROOT)
  message(FATAL_ERROR "Wide-layout runtime requires RAZ_EXE, SOURCE_ROOT, and WORK_ROOT")
endif()

file(REMOVE_RECURSE "${WORK_ROOT}")
file(MAKE_DIRECTORY
  "${WORK_ROOT}/src/core/option"
  "${WORK_ROOT}/src/core/iter"
  "${WORK_ROOT}/src/core/hash"
  "${WORK_ROOT}/src/core/mem"
  "${WORK_ROOT}/src/alloc/box"
  "${WORK_ROOT}/src/alloc/vec"
  "${WORK_ROOT}/src/alloc/deque"
  "${WORK_ROOT}/src/collections/vector"
  "${WORK_ROOT}/src/collections/deque"
  "${WORK_ROOT}/src/collections/hash_set"
  "${WORK_ROOT}/src/collections/hash_map")

foreach(source_file
    library/core/option/option.rz
    library/core/iter/iterator.rz
    library/core/hash/hash.rz
    library/core/mem/mem.rz
    library/alloc/box/box.rz
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
name = "passj_wide_layout"
version = "1.0.0"
kind = "executable"
entry = "src/main.rz"
]=])

file(WRITE "${WORK_ROOT}/src/main.rz" [=[
import core::mem;
import alloc::box;
import collections::vector;
import collections::deque;
import collections::hash_set;
import collections::hash_map;
import core::hash;

extern fn raz_rt_stage1_arena_create(i64 count) -> i64;
extern fn raz_rt_stage1_arena_destroy(i64 handle);
extern fn raz_rt_stage1_arena_get(i64 handle, i64 index) -> i64;
extern fn raz_rt_stage1_arena_set(i64 handle, i64 index, i64 value);

struct Triple { i64 a; i64 b; i64 c; }
@align(32)struct AlignedTriple { i64 a; i64 b; i64 c; }
@align(4096)struct PageAligned { i64 value; }
@align(32)struct OwnedAligned { i64 tracker; i64 value; i64 pad; }

impl Drop for OwnedAligned {
    fn drop(OwnedAligned&mut self) {
        i64 count = raz_rt_stage1_arena_get(self.tracker, 0);
        raz_rt_stage1_arena_set(self.tracker, 0, count + 1);
    }
}

impl Hash for Triple {
    fn hash(Triple& self) -> i64 {
        return (self.a * 31 + self.b * 17 + self.c) & 9223372036854775807;
    }
}
impl Eq for Triple {
    fn equals(Triple& self, Triple& other) -> bool {
        return self.a == other.a && self.b == other.b && self.c == other.c;
    }
}

fn exercise_owned_vector(i64 tracker) -> i64 {
    Vector<OwnedAligned> values = Vector<OwnedAligned>::new();
    i64 index = 0;
    while (index < 20) {
        if (!values.push(OwnedAligned(tracker, index, index + 1))) { return 1; }
        index += 1;
    }
    return 0;
}

fn exercise_owned_map(i64 tracker) -> i64 {
    HashMap<Triple, OwnedAligned> values = HashMap<Triple, OwnedAligned>::new();
    i64 index = 0;
    while (index < 20) {
        if (!values.insert(Triple(index, 0, 0), OwnedAligned(tracker, index, index + 1))) { return 1; }
        index += 1;
    }
    if (!values.insert(Triple(2, 0, 0), OwnedAligned(tracker, 100, 101))) { return 2; }
    return 0;
}

fn main() -> i64 {
    Layout triple_layout = layout_of<Triple>();
    Layout aligned_layout = layout_of<AlignedTriple>();
    if (triple_layout.size != 24 || triple_layout.alignment != 8) { return 1; }
    if (aligned_layout.size != 32 || aligned_layout.alignment != 32) { return 2; }
    if (layout_array_bytes(&aligned_layout, 3) != 96) { return 3; }

    usize raw = allocate_type<AlignedTriple>();
    if (raw == 0 || raw % 32 != 0) { return 4; }
    unsafe {
        *(raw as AlignedTriple*mut) = AlignedTriple(90, 91, 92);
        if ((*(raw as AlignedTriple*const)).b != 91) { return 5; }
    }
    deallocate_type<AlignedTriple>(raw);
    usize page = allocate_type<PageAligned>();
    if (page == 0 || page % 4096 != 0) { return 6; }
    deallocate_type<PageAligned>(page);

    Vector<Triple> vector = Vector<Triple>::new();
    i64 index = 0;
    while (index < 20) {
        if (!vector.push(Triple(index, index + 1, index + 2))) { return 7; }
        index += 1;
    }
    i64 vector_sum = 0;
    for item in vector { vector_sum += item.a + item.b + item.c; }
    if (vector_sum != 630 || vector.capacity() < 20) { return 8; }

    Vector<AlignedTriple> aligned = Vector<AlignedTriple>::new();
    index = 0;
    while (index < 20) {
        if (!aligned.push(AlignedTriple(index, index + 10, index + 20))) { return 9; }
        index += 1;
    }
    unsafe {
        index = 0;
        while (index < 20) {
            usize address = aligned.get_ptr(index) as usize;
            if (address == 0 || address % 32 != 0) { return 10; }
            if ((*aligned.get_ptr(index)).b != index + 10) { return 11; }
            index += 1;
        }
    }

    Deque<Triple> deque = Deque<Triple>::new();
    index = 0;
    while (index < 20) {
        if (!deque.push_back(Triple(index, index + 1, index + 2))) { return 12; }
        index += 1;
    }
    i64 deque_sum = 0;
    for item in deque { deque_sum += item.a + item.b + item.c; }
    if (deque_sum != 630 || deque.capacity() < 20) { return 13; }

    HashSet<Triple> set = HashSet<Triple>::new();
    index = 0;
    while (index < 20) {
        if (!set.insert(Triple(index, index + 1, index + 2))) { return 14; }
        index += 1;
    }
    if (set.insert(Triple(4, 5, 6))) { return 15; }
    Triple probe = Triple(19, 20, 21);
    if (!set.contains(&probe) || set.len() != 20 || set.capacity() < 20) { return 16; }
    i64 set_sum = 0;
    for item in set { set_sum += item.a; }
    if (set_sum != 190) { return 17; }

    HashMap<Triple, AlignedTriple> map = HashMap<Triple, AlignedTriple>::new();
    index = 0;
    while (index < 20) {
        if (!map.insert(Triple(index, 0, 0), AlignedTriple(index, index + 10, index + 20))) { return 18; }
        index += 1;
    }
    if (!map.insert(Triple(2, 0, 0), AlignedTriple(100, 110, 120))) { return 19; }
    Triple key = Triple(2, 0, 0);
    unsafe {
        usize value_address = map.get_ptr(&key) as usize;
        if (value_address == 0 || value_address % 32 != 0) { return 20; }
        if ((*map.get_ptr(&key)).b != 110) { return 21; }
    }
    i64 entry_sum = 0;
    for entry in map.entries() {
        AlignedTriple& current = entry.value();
        entry_sum += current.a;
    }
    if (entry_sum != 288 || map.len() != 20 || map.capacity() < 20) { return 22; }

    i64 tracker = raz_rt_stage1_arena_create(1);
    if (tracker == 0) { return 23; }
    if (exercise_owned_vector(tracker) != 0) { return 24; }
    if (raz_rt_stage1_arena_get(tracker, 0) != 20) { return 25; }
    if (exercise_owned_map(tracker) != 0) { return 26; }
    // 20 vector values + 20 final map values + one replaced map value.
    if (raz_rt_stage1_arena_get(tracker, 0) != 41) { return 27; }
    raz_rt_stage1_arena_destroy(tracker);
    return 0;
}
]=])

execute_process(
  COMMAND "${RAZ_EXE}" build "${WORK_ROOT}" --target test-host --force
  RESULT_VARIABLE build_result OUTPUT_VARIABLE build_output ERROR_VARIABLE build_error)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "Wide-layout project failed to build:\n${build_error}\n${build_output}")
endif()
if(WIN32)
  set(runtime_exe "${WORK_ROOT}/target/test-host/debug/passj_wide_layout.exe")
else()
  set(runtime_exe "${WORK_ROOT}/target/test-host/debug/passj_wide_layout")
endif()
if(NOT EXISTS "${runtime_exe}")
  message(FATAL_ERROR "Wide-layout executable missing: ${runtime_exe}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env TERM=xterm "${runtime_exe}"
  RESULT_VARIABLE runtime_result OUTPUT_VARIABLE runtime_output ERROR_VARIABLE runtime_error)
if(NOT runtime_result EQUAL 0)
  message(FATAL_ERROR "Wide-layout runtime returned ${runtime_result}:\n${runtime_error}\n${runtime_output}")
endif()
