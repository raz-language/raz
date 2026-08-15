# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED RAZ_EXE OR NOT DEFINED WORK_ROOT)
  message(FATAL_ERROR "Hexadecimal FIR round-trip test requires RAZ_EXE and WORK_ROOT")
endif()
file(REMOVE_RECURSE "${WORK_ROOT}")
file(MAKE_DIRECTORY "${WORK_ROOT}/src")
file(WRITE "${WORK_ROOT}/raz.toml" [=[
[package]
name = "passl_hex_roundtrip"
version = "1.0.0"
kind = "executable"
entry = "src/main.rz"
]=])
file(WRITE "${WORK_ROOT}/src/main.rz" [=[
fn main() -> i64 {
    u16 small = 0x1234 as u16;
    u32 wide = 0x89abcdef as u32;
    if (small != (0x1234 as u16)) { return 1; }
    if (wide != (0x89abcdef as u32)) { return 2; }
    return 0;
}
]=])
execute_process(COMMAND "${RAZ_EXE}" build "${WORK_ROOT}" --target test-host --force
  RESULT_VARIABLE build_result OUTPUT_VARIABLE build_output ERROR_VARIABLE build_error)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "Hexadecimal FIR round-trip build failed:\n${build_error}\n${build_output}")
endif()
if(WIN32)
  set(runtime_exe "${WORK_ROOT}/target/test-host/debug/passl_hex_roundtrip.exe")
else()
  set(runtime_exe "${WORK_ROOT}/target/test-host/debug/passl_hex_roundtrip")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env TERM=xterm "${runtime_exe}"
  RESULT_VARIABLE runtime_result OUTPUT_VARIABLE runtime_output ERROR_VARIABLE runtime_error)
if(NOT runtime_result EQUAL 0)
  message(FATAL_ERROR "Hexadecimal FIR round-trip runtime returned ${runtime_result}:\n${runtime_error}\n${runtime_output}")
endif()
