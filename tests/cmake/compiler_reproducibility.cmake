# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED RAZ_EXE OR NOT DEFINED RAZ_RUNTIME_LIB OR NOT DEFINED RAZ_FORGE_BRIDGE_LIB OR
   NOT DEFINED FORGE_LIB OR NOT DEFINED CXX_COMPILER OR NOT DEFINED SOURCE_ROOT OR NOT DEFINED WORK_ROOT)
  message(FATAL_ERROR "RAZ_EXE, RAZ_RUNTIME_LIB, RAZ_FORGE_BRIDGE_LIB, FORGE_LIB, CXX_COMPILER, SOURCE_ROOT, and WORK_ROOT are required")
endif()

set(raz_runtime_link_deps "")
if(DEFINED RAZ_RUNTIME_LINK_DEPS AND NOT "${RAZ_RUNTIME_LINK_DEPS}" STREQUAL "")
  set(raz_runtime_link_deps ${RAZ_RUNTIME_LINK_DEPS})
endif()

include("${SOURCE_ROOT}/tests/cmake/compiler_source_set.cmake")

file(REMOVE_RECURSE "${WORK_ROOT}")
file(MAKE_DIRECTORY "${WORK_ROOT}")
file(COPY "${SOURCE_ROOT}/compiler" DESTINATION "${WORK_ROOT}")
set(project "${WORK_ROOT}/compiler")
set(frontend "${project}/compiler-compiler.rz")
materialize_compiler_source("${SOURCE_ROOT}" "${frontend}")

execute_process(
  COMMAND "${RAZ_EXE}" build "${project}" --profile debug --force
  RESULT_VARIABLE candidate_build_result
  OUTPUT_VARIABLE candidate_build_output
  ERROR_VARIABLE candidate_build_error)
if(NOT candidate_build_result EQUAL 0)
  message(FATAL_ERROR "Recursive compiler gate could not build compiler candidate:\n${candidate_build_output}\n${candidate_build_error}")
endif()

if(WIN32)
  set(candidate_executable "${project}/target/debug/bin/raz-compiler.exe")
  set(production_object "${project}/production-output.obj")
  set(production_executable "${WORK_ROOT}/production.exe")
else()
  set(candidate_executable "${project}/target/debug/bin/raz-compiler")
  set(production_object "${project}/production-output.o")
  set(production_executable "${WORK_ROOT}/production")
endif()

file(COPY_FILE "${frontend}" "${project}/compiler-candidate.rz" ONLY_IF_DIFFERENT)
file(WRITE "${project}/compiler-package.txt" "compiler-candidate.rz\n")
set(production_ir "${project}/production-output.fir")
execute_process(
  COMMAND "${candidate_executable}" --forge-native "compiler-package.txt" "production-output.fir"
  WORKING_DIRECTORY "${project}"
  RESULT_VARIABLE candidate_run_result)
if(NOT candidate_run_result EQUAL 0)
  message(FATAL_ERROR "Recursive compiler compiler candidate explicit-path CLI returned ${candidate_run_result} while emitting production compiler")
endif()

if(NOT EXISTS "${production_ir}")
  message(FATAL_ERROR "Recursive compiler compiler candidate did not emit production compiler Forge IR")
endif()
file(SIZE "${production_ir}" production_ir_size)
if(production_ir_size LESS 500000)
  message(FATAL_ERROR "Recursive compiler production compiler Forge module is unexpectedly small: ${production_ir_size} bytes")
endif()

if(NOT EXISTS "${production_object}")
  message(FATAL_ERROR "Recursive compiler compiler candidate in-process Forge backend did not emit production compiler object")
endif()
if(WIN32)
  execute_process(
    COMMAND "${CXX_COMPILER}" "${production_object}" "${RAZ_RUNTIME_LIB}" ${raz_runtime_link_deps} "${RAZ_FORGE_BRIDGE_LIB}" "${FORGE_LIB}" -o "${production_executable}"
    RESULT_VARIABLE production_link_result
    OUTPUT_VARIABLE production_link_output
    ERROR_VARIABLE production_link_error)
else()
  execute_process(
    COMMAND "${CXX_COMPILER}" "${production_object}" "${RAZ_RUNTIME_LIB}" ${raz_runtime_link_deps} "${RAZ_FORGE_BRIDGE_LIB}" "${FORGE_LIB}" -pthread -o "${production_executable}"
    RESULT_VARIABLE production_link_result
    OUTPUT_VARIABLE production_link_output
    ERROR_VARIABLE production_link_error)
endif()
if(NOT production_link_result EQUAL 0 OR NOT EXISTS "${production_executable}")
  message(FATAL_ERROR "Recursive compiler production compiler link failed:\n${production_link_output}\n${production_link_error}")
endif()

set(verification_work "${WORK_ROOT}/verification")
file(MAKE_DIRECTORY "${verification_work}")
file(COPY_FILE "${frontend}" "${verification_work}/compiler.rz" ONLY_IF_DIFFERENT)
file(WRITE "${verification_work}/compiler-package.txt" "compiler.rz\n")
set(verification_ir "${verification_work}/verification-output.fir")
execute_process(
  COMMAND "${production_executable}" "compiler-package.txt" "verification-output.fir"
  WORKING_DIRECTORY "${verification_work}"
  RESULT_VARIABLE production_run_result)
if(NOT production_run_result EQUAL 0)
  message(FATAL_ERROR "Recursive compiler production compiler explicit-path CLI returned ${production_run_result} while emitting verification compiler")
endif()

if(NOT EXISTS "${verification_ir}")
  message(FATAL_ERROR "Recursive compiler production compiler did not emit verification compiler Forge IR")
endif()
file(SIZE "${verification_ir}" verification_ir_size)
if(verification_ir_size LESS 500000)
  message(FATAL_ERROR "Recursive compiler verification compiler Forge module is unexpectedly small: ${verification_ir_size} bytes")
endif()

file(SHA256 "${production_ir}" production_hash)
file(SHA256 "${verification_ir}" verification_hash)
if(NOT production_hash STREQUAL verification_hash OR NOT production_ir_size EQUAL verification_ir_size)
  message(FATAL_ERROR
    "Recursive compilering did not reach a deterministic fixed point:\n"
    "  production compiler: ${production_ir_size} bytes ${production_hash}\n"
    "  verification compiler: ${verification_ir_size} bytes ${verification_hash}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E compare_files "${production_ir}" "${verification_ir}"
  RESULT_VARIABLE compare_result)
if(NOT compare_result EQUAL 0)
  message(FATAL_ERROR "Recursive compiler production compiler and verification compiler Forge modules differ despite matching metadata")
endif()

message(STATUS
  "Recursive compiler fixed point complete: production compiler and verification compiler are byte-identical (${production_ir_size} bytes, SHA-256 ${production_hash})")
