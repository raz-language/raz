# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED RAZ_EXE OR NOT DEFINED FORGE_CODEGEN OR NOT DEFINED RAZ_RUNTIME_LIB OR
   NOT DEFINED CXX_COMPILER OR NOT DEFINED SOURCE_ROOT OR NOT DEFINED WORK_ROOT)
  message(FATAL_ERROR "RAZ_EXE, FORGE_CODEGEN, RAZ_RUNTIME_LIB, CXX_COMPILER, SOURCE_ROOT, and WORK_ROOT are required")
endif()

if(NOT DEFINED RAZ_RUNTIME_LINK_MANIFEST OR NOT EXISTS "${RAZ_RUNTIME_LINK_MANIFEST}")
  message(FATAL_ERROR "Raz runtime link manifest is required: ${RAZ_RUNTIME_LINK_MANIFEST}")
endif()
file(STRINGS "${RAZ_RUNTIME_LINK_MANIFEST}" raz_runtime_link_deps)
list(FILTER raz_runtime_link_deps EXCLUDE REGEX "^[ \t]*$")

file(REMOVE_RECURSE "${WORK_ROOT}")
file(MAKE_DIRECTORY "${WORK_ROOT}")
file(COPY "${SOURCE_ROOT}/compiler" DESTINATION "${WORK_ROOT}")
set(project "${WORK_ROOT}/compiler")

# Build the Raz-written compiler with the native host compiler.  The bug this
# test guards against lived in the Raz compiler's HIR -> MIR lowering, so the
# generated compiler must be the component compiling the fixture below.
execute_process(
  COMMAND "${RAZ_EXE}" build "${project}" --profile debug --force
  RESULT_VARIABLE candidate_build_result
  OUTPUT_VARIABLE candidate_build_output
  ERROR_VARIABLE candidate_build_error)
if(NOT candidate_build_result EQUAL 0)
  message(FATAL_ERROR "Could not build the Raz-written compiler:\n${candidate_build_output}\n${candidate_build_error}")
endif()

if(WIN32)
  set(candidate_executable "${project}/target/debug/bin/raz-compiler.exe")
  set(object "${WORK_ROOT}/mutable-parameter.obj")
  set(executable "${WORK_ROOT}/mutable-parameter.exe")
else()
  set(candidate_executable "${project}/target/debug/bin/raz-compiler")
  set(object "${WORK_ROOT}/mutable-parameter.o")
  set(executable "${WORK_ROOT}/mutable-parameter")
endif()
if(NOT EXISTS "${candidate_executable}")
  message(FATAL_ERROR "Generated Raz compiler is missing: ${candidate_executable}")
endif()

# Mutate a parameter through direct, compound, and loop assignments.  Before
# the regression fix the lowering computed each RHS but silently discarded the
# store for HIR parameter targets, turning the while loop into an infinite loop.
file(WRITE "${WORK_ROOT}/mutable-parameter.rz" [=[
// Force the complete HIR/MIR parser rather than the compact scalar parser.
public struct MutableParameterProbe { i64 value; }

public fn advance(i64 value) -> i64 {
    value = value + 0;
    value += 1;
    value -= 1;
    while value < 42 {
        value += 1;
    }
    return value;
}

public fn main() -> i64 {
    return advance(40);
}
]=])
file(WRITE "${WORK_ROOT}/mutable-parameter.txt" "mutable-parameter.rz\n")
set(forge_ir "${WORK_ROOT}/mutable-parameter.fir")

execute_process(
  COMMAND "${candidate_executable}" "mutable-parameter.txt" "mutable-parameter.fir"
  WORKING_DIRECTORY "${WORK_ROOT}"
  RESULT_VARIABLE compile_result
  OUTPUT_VARIABLE compile_output
  ERROR_VARIABLE compile_error
  TIMEOUT 30)
if(NOT compile_result EQUAL 0 OR NOT EXISTS "${forge_ir}")
  message(FATAL_ERROR "Raz-written compiler failed mutable-parameter fixture (${compile_result}):\n${compile_output}\n${compile_error}")
endif()

# A mutable parameter must be backed by a frame slot in the generated module.
# Keep this structural assertion in addition to executing the artifact so a
# missing store cannot regress into another non-terminating program.
file(READ "${forge_ir}" emitted_ir)
if(NOT emitted_ir MATCHES "call void @raz_compiler_rt_arena_set")
  message(FATAL_ERROR "Mutable parameter lowering emitted no frame store")
endif()

if(WIN32)
  execute_process(
    COMMAND "${FORGE_CODEGEN}" "${forge_ir}" "--emit-coff=${object}" --abi=windows
    RESULT_VARIABLE codegen_result OUTPUT_VARIABLE codegen_output ERROR_VARIABLE codegen_error)
else()
  execute_process(
    COMMAND "${FORGE_CODEGEN}" "${forge_ir}" "--emit-elf=${object}" --abi=sysv
    RESULT_VARIABLE codegen_result OUTPUT_VARIABLE codegen_output ERROR_VARIABLE codegen_error)
endif()
if(NOT codegen_result EQUAL 0 OR NOT EXISTS "${object}")
  message(FATAL_ERROR "Forge could not emit mutable-parameter object:\n${codegen_output}\n${codegen_error}")
endif()

if(WIN32)
  execute_process(
    COMMAND "${CXX_COMPILER}" /nologo "${object}" "${RAZ_RUNTIME_LIB}" ${raz_runtime_link_deps} ws2_32.lib bcrypt.lib "/Fe:${executable}"
    RESULT_VARIABLE link_result OUTPUT_VARIABLE link_output ERROR_VARIABLE link_error)
else()
  execute_process(
    COMMAND "${CXX_COMPILER}" "${object}" "${RAZ_RUNTIME_LIB}" ${raz_runtime_link_deps} -pthread -o "${executable}"
    RESULT_VARIABLE link_result OUTPUT_VARIABLE link_output ERROR_VARIABLE link_error)
endif()
if(NOT link_result EQUAL 0 OR NOT EXISTS "${executable}")
  message(FATAL_ERROR "Could not link mutable-parameter fixture:\n${link_output}\n${link_error}")
endif()

execute_process(COMMAND "${executable}" RESULT_VARIABLE run_result TIMEOUT 10)
if(NOT run_result EQUAL 42)
  message(FATAL_ERROR "Mutable-parameter fixture returned ${run_result}, expected 42")
endif()

message(STATUS "compiler mutable-parameter lowering: PASS")
