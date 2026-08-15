# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED RAZ_EXE OR NOT DEFINED RAZ_RUNTIME_LIB OR NOT DEFINED RAZ_FORGE_BRIDGE_LIB OR
   NOT DEFINED FORGE_LIB OR NOT DEFINED CXX_COMPILER OR NOT DEFINED SOURCE_ROOT OR NOT DEFINED WORK_ROOT)
  message(FATAL_ERROR "RAZ_EXE, RAZ_RUNTIME_LIB, RAZ_FORGE_BRIDGE_LIB, FORGE_LIB, CXX_COMPILER, SOURCE_ROOT, and WORK_ROOT are required")
endif()

if(NOT DEFINED RAZ_RUNTIME_LINK_MANIFEST OR NOT EXISTS "${RAZ_RUNTIME_LINK_MANIFEST}")
  message(FATAL_ERROR "Raz runtime link manifest is required: ${RAZ_RUNTIME_LINK_MANIFEST}")
endif()
file(STRINGS "${RAZ_RUNTIME_LINK_MANIFEST}" raz_runtime_link_deps)
list(FILTER raz_runtime_link_deps EXCLUDE REGEX "^[ \t]*$")

include("${SOURCE_ROOT}/tests/cmake/self_host_source_set.cmake")

file(REMOVE_RECURSE "${WORK_ROOT}")
file(MAKE_DIRECTORY "${WORK_ROOT}")
file(COPY "${SOURCE_ROOT}/compiler" DESTINATION "${WORK_ROOT}")
set(project "${WORK_ROOT}/compiler")
set(frontend "${project}/selfhost-compiler.rz")
materialize_selfhost_source("${SOURCE_ROOT}" "${frontend}")

execute_process(
  COMMAND "${RAZ_EXE}" build "${project}" --target host --profile debug --force
  RESULT_VARIABLE stage1_build_result
  OUTPUT_VARIABLE stage1_build_output
  ERROR_VARIABLE stage1_build_error)
if(NOT stage1_build_result EQUAL 0)
  message(FATAL_ERROR "Recursive self-host gate could not build Stage 1:\n${stage1_build_output}\n${stage1_build_error}")
endif()

if(WIN32)
  set(stage1_executable "${project}/target/host/debug/raz-compiler.exe")
  set(stage2_object "${project}/stage2-output.obj")
  set(stage2_executable "${WORK_ROOT}/stage2.exe")
else()
  set(stage1_executable "${project}/target/host/debug/raz-compiler")
  set(stage2_object "${project}/stage2-output.o")
  set(stage2_executable "${WORK_ROOT}/stage2")
endif()

file(COPY_FILE "${frontend}" "${project}/stage1-compiler.rz" ONLY_IF_DIFFERENT)
file(WRITE "${project}/stage1-package.txt" "stage1-compiler.rz\n")
set(stage2_ir "${project}/stage2-output.fir")
execute_process(
  COMMAND "${stage1_executable}" --forge-native "stage1-package.txt" "stage2-output.fir"
  WORKING_DIRECTORY "${project}"
  RESULT_VARIABLE stage1_run_result)
if(NOT stage1_run_result EQUAL 0)
  message(FATAL_ERROR "Recursive self-host Stage 1 explicit-path CLI returned ${stage1_run_result} while emitting Stage 2")
endif()

if(NOT EXISTS "${stage2_ir}")
  message(FATAL_ERROR "Recursive self-host Stage 1 did not emit Stage 2 Forge IR")
endif()
file(SIZE "${stage2_ir}" stage2_ir_size)
if(stage2_ir_size LESS 500000)
  message(FATAL_ERROR "Recursive self-host Stage 2 Forge module is unexpectedly small: ${stage2_ir_size} bytes")
endif()

if(NOT EXISTS "${stage2_object}")
  message(FATAL_ERROR "Recursive self-host Stage 1 in-process Forge backend did not emit Stage 2 object")
endif()
if(WIN32)
  execute_process(
    COMMAND "${CXX_COMPILER}" "${stage2_object}" "${RAZ_RUNTIME_LIB}" ${raz_runtime_link_deps} "${RAZ_FORGE_BRIDGE_LIB}" "${FORGE_LIB}" -o "${stage2_executable}"
    RESULT_VARIABLE stage2_link_result
    OUTPUT_VARIABLE stage2_link_output
    ERROR_VARIABLE stage2_link_error)
else()
  execute_process(
    COMMAND "${CXX_COMPILER}" "${stage2_object}" "${RAZ_RUNTIME_LIB}" ${raz_runtime_link_deps} "${RAZ_FORGE_BRIDGE_LIB}" "${FORGE_LIB}" -pthread -o "${stage2_executable}"
    RESULT_VARIABLE stage2_link_result
    OUTPUT_VARIABLE stage2_link_output
    ERROR_VARIABLE stage2_link_error)
endif()
if(NOT stage2_link_result EQUAL 0 OR NOT EXISTS "${stage2_executable}")
  message(FATAL_ERROR "Recursive self-host Stage 2 link failed:\n${stage2_link_output}\n${stage2_link_error}")
endif()

set(stage3_work "${WORK_ROOT}/stage3")
file(MAKE_DIRECTORY "${stage3_work}")
file(COPY_FILE "${frontend}" "${stage3_work}/compiler.rz" ONLY_IF_DIFFERENT)
file(WRITE "${stage3_work}/stage1-package.txt" "compiler.rz\n")
set(stage3_ir "${stage3_work}/stage3-output.fir")
execute_process(
  COMMAND "${stage2_executable}" "stage1-package.txt" "stage3-output.fir"
  WORKING_DIRECTORY "${stage3_work}"
  RESULT_VARIABLE stage2_run_result)
if(NOT stage2_run_result EQUAL 0)
  message(FATAL_ERROR "Recursive self-host Stage 2 explicit-path CLI returned ${stage2_run_result} while emitting Stage 3")
endif()

if(NOT EXISTS "${stage3_ir}")
  message(FATAL_ERROR "Recursive self-host Stage 2 did not emit Stage 3 Forge IR")
endif()
file(SIZE "${stage3_ir}" stage3_ir_size)
if(stage3_ir_size LESS 500000)
  message(FATAL_ERROR "Recursive self-host Stage 3 Forge module is unexpectedly small: ${stage3_ir_size} bytes")
endif()

file(SHA256 "${stage2_ir}" stage2_hash)
file(SHA256 "${stage3_ir}" stage3_hash)
if(NOT stage2_hash STREQUAL stage3_hash OR NOT stage2_ir_size EQUAL stage3_ir_size)
  message(FATAL_ERROR
    "Recursive self-hosting did not reach a deterministic fixed point:\n"
    "  Stage 2: ${stage2_ir_size} bytes ${stage2_hash}\n"
    "  Stage 3: ${stage3_ir_size} bytes ${stage3_hash}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E compare_files "${stage2_ir}" "${stage3_ir}"
  RESULT_VARIABLE compare_result)
if(NOT compare_result EQUAL 0)
  message(FATAL_ERROR "Recursive self-host Stage 2 and Stage 3 Forge modules differ despite matching metadata")
endif()

message(STATUS
  "Recursive self-host fixed point complete: Stage 2 and Stage 3 are byte-identical (${stage2_ir_size} bytes, SHA-256 ${stage2_hash})")
