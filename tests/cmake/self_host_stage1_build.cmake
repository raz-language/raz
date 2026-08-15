# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED RAZ_EXE OR NOT DEFINED RAZC_EXE OR NOT DEFINED SOURCE_ROOT OR NOT DEFINED WORK_ROOT)
  message(FATAL_ERROR "RAZ_EXE, RAZC_EXE, SOURCE_ROOT, and WORK_ROOT are required")
endif()

include("${SOURCE_ROOT}/tests/cmake/self_host_source_set.cmake")

file(REMOVE_RECURSE "${WORK_ROOT}")
file(MAKE_DIRECTORY "${WORK_ROOT}")
file(COPY "${SOURCE_ROOT}/compiler" DESTINATION "${WORK_ROOT}")
set(project "${WORK_ROOT}/compiler")
set(frontend "${project}/selfhost-compiler.rz")
materialize_selfhost_source("${SOURCE_ROOT}" "${frontend}")

# Stage 1 build proof: Stage 0 must accept and lower the entire Raz-owned
# bootstrap compiler, not a reduced fixture or native replacement.
execute_process(COMMAND "${RAZC_EXE}" --check "${frontend}"
  RESULT_VARIABLE check_result OUTPUT_VARIABLE check_output ERROR_VARIABLE check_error)
if(NOT check_result EQUAL 0)
  message(FATAL_ERROR "Stage 1 semantic qualification failed:\n${check_output}\n${check_error}")
endif()

execute_process(COMMAND "${RAZC_EXE}" --forge-ir "${frontend}"
  RESULT_VARIABLE ir_result OUTPUT_VARIABLE ir_output ERROR_VARIABLE ir_error)
if(NOT ir_result EQUAL 0)
  message(FATAL_ERROR "Stage 1 Forge lowering failed:\n${ir_output}\n${ir_error}")
endif()

foreach(symbol next_token parse_module build_hir lower_hir execute_mir_main emit_forge_module main)
  if(NOT ir_output MATCHES "func @${symbol}")
    message(FATAL_ERROR "Stage 1 Forge IR is missing required compiler function @${symbol}")
  endif()
endforeach()

# Build the exact compiler source as the package entry point. This creates the
# native Stage 1 compiler artifact used by the next bootstrap pass.
execute_process(COMMAND "${RAZ_EXE}" build "${project}" --target host --profile debug --force
  RESULT_VARIABLE build_result OUTPUT_VARIABLE build_output ERROR_VARIABLE build_error)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "Native Stage 1 compiler build failed:\n${build_output}\n${build_error}")
endif()

if(WIN32)
  set(executable "${project}/target/host/debug/raz-compiler.exe")
else()
  set(executable "${project}/target/host/debug/raz-compiler")
endif()
if(NOT EXISTS "${executable}")
  message(FATAL_ERROR "Native Stage 1 compiler artifact missing: ${executable}")
endif()

file(SIZE "${executable}" executable_size)
if(executable_size LESS 4096)
  message(FATAL_ERROR "Stage 1 compiler artifact is unexpectedly small: ${executable_size} bytes")
endif()

message(STATUS "Stage 1 build complete: Stage 0 produced native Stage 1 compiler (${executable_size} bytes)")
