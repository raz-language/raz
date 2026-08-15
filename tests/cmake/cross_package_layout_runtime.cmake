# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED RAZ_EXE OR NOT DEFINED WORK_ROOT)
  message(FATAL_ERROR "Cross-package layout test requires RAZ_EXE and WORK_ROOT")
endif()
file(REMOVE_RECURSE "${WORK_ROOT}")
file(MAKE_DIRECTORY "${WORK_ROOT}/layoutlib/src" "${WORK_ROOT}/consumer/src")
file(WRITE "${WORK_ROOT}/layoutlib/raz.toml" [=[
[package]
name = "layoutlib"
version = "1.0.0"
kind = "static-library"
source = "src"
]=])
file(WRITE "${WORK_ROOT}/layoutlib/src/layout.rz" [=[
namespace layoutlib;
public struct TypeLayout { i64 size; i64 alignment; }
public fn type_layout<T>() -> TypeLayout {
    return TypeLayout(size_of<T>() as i64, align_of<T>() as i64);
}
]=])
file(WRITE "${WORK_ROOT}/consumer/raz.toml" [=[
[package]
name = "layoutconsumer"
version = "1.0.0"
kind = "executable"
entry = "src/main.rz"
[dependencies]
layoutlib = "../layoutlib"
]=])
file(WRITE "${WORK_ROOT}/consumer/src/main.rz" [=[
import layoutlib::layoutlib;
@align(64)struct Payload { i64 a; i64 b; i64 c; }
fn main() -> i64 {
    TypeLayout layout = type_layout<Payload>();
    if (layout.size != 64) { return 1; }
    if (layout.alignment != 64) { return 2; }
    return 0;
}
]=])
execute_process(COMMAND "${RAZ_EXE}" build "${WORK_ROOT}/consumer" --target test-host --force
  RESULT_VARIABLE build_result OUTPUT_VARIABLE build_output ERROR_VARIABLE build_error)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "Cross-package generic layout failed:\n${build_error}\n${build_output}")
endif()
if(WIN32)
  set(runtime_exe "${WORK_ROOT}/consumer/target/test-host/debug/layoutconsumer.exe")
else()
  set(runtime_exe "${WORK_ROOT}/consumer/target/test-host/debug/layoutconsumer")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env TERM=xterm "${runtime_exe}"
  RESULT_VARIABLE runtime_result OUTPUT_VARIABLE runtime_output ERROR_VARIABLE runtime_error)
if(NOT runtime_result EQUAL 0)
  message(FATAL_ERROR "Cross-package layout runtime returned ${runtime_result}:\n${runtime_error}\n${runtime_output}")
endif()
