# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED RAZ_EXE OR NOT DEFINED SOURCE_ROOT OR NOT DEFINED WORK_ROOT)
  message(FATAL_ERROR "Hosted OS runtime requires RAZ_EXE, SOURCE_ROOT, WORK_ROOT")
endif()
include("${SOURCE_ROOT}/tests/cmake/copy_stdlib_closure.cmake")
file(REMOVE_RECURSE "${WORK_ROOT}")
file(MAKE_DIRECTORY "${WORK_ROOT}/src")
file(WRITE "${WORK_ROOT}/raz.toml" [=[
[package]
name = "hosted_os_runtime_fixture"
version = "1.0.0"
kind = "executable"
entry = "src/main.rz"
]=])
file(WRITE "${WORK_ROOT}/src/main.rz" [=[
import alloc::string;
import core::result;
import std::io::error;
import std::fs;
import std::fs::metadata;
import std::fs::bytes;
import std::fs::owned;
import std::path::buf;
import std::io::buffer;
import std::fs::read_dir;
import std::fs::tree;
import std::path;
import std::process::command;
import std::thread;
import std::sync::mutex;
import std::sync::rwlock;

fn put(usize out, i64 index, i64 value) { raz_rt_store_u8(out + index, value); }
fn main() -> i64 {
    usize dir = raz_rt_alloc(10); i64 db[10] = [112,97,115,115,95,112,95,116,109,112];
    i64 i = 0; while (i < 10) { put(dir, i, db[i]); i += 1; }
    std::fs::remove(dir, 10); if (!std::fs::create_dir(dir, 10)) { return 1; }

    usize file = raz_rt_alloc(21); i64 fb[21] = [112,97,115,115,95,112,95,116,109,112,47,115,97,109,112,108,101,46,116,120,116];
    i = 0; while (i < 21) { put(file, i, fb[i]); i += 1; }
    usize data = raz_rt_alloc(5); i64 bytes[5] = [104,101,108,108,111];
    i = 0; while (i < 5) { put(data, i, bytes[i]); i += 1; }
    if (std::fs::write(file, 21, data, 5) != 5) { return 2; }

    String file_string = alloc::string::from_bytes(file, 21);
    if (!std::fs::owned::exists_string(&file_string) || !std::fs::owned::is_file_string(&file_string) || std::fs::owned::file_size_string(&file_string) != 5) { return 18; }
    PathBuf file_path = path_buf_from_bytes(file, 21);
    if (!std::fs::owned::exists_path(&file_path) || !std::fs::owned::is_file_path(&file_path) || std::fs::owned::file_size_path(&file_path) != 5) { return 19; }
    Result<ByteBuffer, IoError> binary_result = std::fs::bytes::read_all(file, 21);
    match binary_result {
      Result<ByteBuffer, IoError>::Error(_) => { return 16; }
      Result<ByteBuffer, IoError>::Ok(buffer) => {
        ByteBuffer owned = move buffer;
        if (owned.len() != 5 || owned.peek(0) != 104 || owned.peek(4) != 111) { return 17; }
      }
    }

    Result<Metadata, IoError> meta_result = std::fs::metadata::query(file, 21);
    match meta_result { Result<Metadata, IoError>::Error(_) => { return 3; } Result<Metadata, IoError>::Ok(meta) => { if (!std::fs::metadata::is_file(&meta) || meta.size != 5) { return 4; } } }

    Result<ReadDir, IoError> opened = std::fs::read_dir::open(dir, 10);
    match opened {
      Result<ReadDir, IoError>::Error(_) => { return 5; }
      Result<ReadDir, IoError>::Ok(iter) => {
        ReadDir rd = move iter; String name = alloc::string::with_capacity(4); Metadata em = Metadata { kind: 0, size: 0, modified_millis: 0, readonly: false, symlink: false, exists: false };
        if (std::fs::read_dir::next(&mut rd, &mut name, &mut em) != 1) { return 6; }
        if (!alloc::string::equals_bytes(&name, file + 11, 10) || em.size != 5) { return 7; }
        if (std::fs::read_dir::next(&mut rd, &mut name, &mut em) != 0) { return 8; }
      }
    }

    Mutex mutex = std::sync::mutex::create();
    { MutexGuard guard = std::sync::mutex::guard(&mutex); if (!std::sync::mutex::guard_locked(&guard)) { return 9; } }
    if (!std::sync::mutex::try_lock(&mutex)) { return 10; } std::sync::mutex::unlock(&mutex);
    RwLock rw = std::sync::rwlock::create();
    { ReadGuard rg = std::sync::rwlock::read_guard(&rw); }
    { WriteGuard wg = std::sync::rwlock::try_write_guard(&rw); if (!wg.locked) { return 11; } }

    std::fs::remove(file, 21); std::fs::remove(dir, 10);
    usize tree = raz_rt_alloc(15); i64 tb[15] = [112,97,115,115,95,112,95,116,114,101,101,47,97,47,98];
    i = 0; while(i < 15){ put(tree,i,tb[i]); i += 1; }
    Result<bool,IoError> made = std::fs::tree::create_dir_all(tree,15);
    match made { Result<bool,IoError>::Error(_) => { return 12; } Result<bool,IoError>::Ok(v) => { if(!v){return 13;} } }
    usize root = raz_rt_alloc(11); i=0; while(i<11){ put(root,i,tb[i]); i += 1; }
    Result<i64,IoError> removed = std::fs::tree::remove_dir_all(root,11);
    match removed { Result<i64,IoError>::Error(_) => { return 14; } Result<i64,IoError>::Ok(count) => { if(count < 3){return 15;} } }

    raz_rt_dealloc(root); raz_rt_dealloc(tree); raz_rt_dealloc(data); raz_rt_dealloc(file); raz_rt_dealloc(dir);
    return 0;
}
]=])
raz_copy_stdlib_closure()

execute_process(COMMAND "${RAZ_EXE}" build "${WORK_ROOT}" --target test-host --profile debug --force
  RESULT_VARIABLE build_result OUTPUT_VARIABLE build_output ERROR_VARIABLE build_error)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "Hosted OS build failed:\n${build_error}\n${build_output}")
endif()
if(WIN32)
  set(runtime_exe "${WORK_ROOT}/target/test-host/debug/hosted_os_runtime_fixture.exe")
else()
  set(runtime_exe "${WORK_ROOT}/target/test-host/debug/hosted_os_runtime_fixture")
endif()
execute_process(COMMAND "${runtime_exe}" WORKING_DIRECTORY "${WORK_ROOT}"
  RESULT_VARIABLE runtime_result OUTPUT_VARIABLE runtime_output ERROR_VARIABLE runtime_error)
if(NOT runtime_result EQUAL 0)
  message(FATAL_ERROR "Hosted OS runtime returned ${runtime_result}:\n${runtime_error}\n${runtime_output}")
endif()
