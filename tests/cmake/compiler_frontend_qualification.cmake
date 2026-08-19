# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED RAZ_EXE OR NOT DEFINED RAZC_EXE OR NOT DEFINED FORGE_CODEGEN OR NOT DEFINED SOURCE_ROOT OR NOT DEFINED WORK_ROOT)
  message(FATAL_ERROR "RAZ_EXE, RAZC_EXE, FORGE_CODEGEN, SOURCE_ROOT, and WORK_ROOT are required")
endif()

include("${SOURCE_ROOT}/tests/cmake/compiler_source_set.cmake")
file(REMOVE_RECURSE "${WORK_ROOT}")
file(COPY "${SOURCE_ROOT}/compiler" DESTINATION "${WORK_ROOT}")
set(project "${WORK_ROOT}/compiler")
set(frontend "${project}/compiler-compiler.rz")
materialize_compiler_source("${SOURCE_ROOT}" "${frontend}")
file(WRITE "${project}/seed-math.rz" "// compiler candidate package math module\npublic fn identity(i64 x) -> i64 { return x; }\npublic fn twice(i64 x) -> i64 { return x * 2; }\npublic fn divide(i64 x, i64 y) -> i64 { return x / y; }\n")
file(WRITE "${project}/seed-combine.rz" "// Reassignment and cross-module calls\npublic fn adjust(i64 x) -> i64 { i64 value = identity(x); value = value + 4; value = value - 1; return value; }\npublic fn combine(i64 a, i64 b, i64 c) -> i64 { return a + b - c; }\npublic fn choose(i64 x) -> i64 { if x >= 40 { return (x + 0); } else { return 0; } }\n")
file(WRITE "${project}/seed-main.rz" "public fn main() -> i64 { i64 base = twice(20); i64 seed = -1; i64 delta = adjust(seed); i64 zero = combine(1, 2, 3); i64 quotient = divide(8, 4); quotient = quotient - 2; return choose(base + delta + zero + quotient); }\n")
file(WRITE "${project}/compiler-package.txt" "seed-math.rz\nseed-combine.rz\nseed-main.rz\n")
execute_process(COMMAND "${RAZC_EXE}" --check "${frontend}"
  RESULT_VARIABLE check_result OUTPUT_VARIABLE check_output ERROR_VARIABLE check_error)
if(NOT check_result EQUAL 0)
  message(FATAL_ERROR "compiler candidate frontend semantic check failed:\n${check_output}\n${check_error}")
endif()
execute_process(COMMAND "${RAZC_EXE}" --forge-ir "${frontend}"
  RESULT_VARIABLE ir_result OUTPUT_VARIABLE ir_output ERROR_VARIABLE ir_error)
if(NOT ir_result EQUAL 0)
  message(FATAL_ERROR "compiler candidate frontend Forge lowering failed:\n${ir_output}\n${ir_error}")
endif()
if(NOT ir_output MATCHES "func @next_token" OR NOT ir_output MATCHES "func @token_count" OR NOT ir_output MATCHES "func @parse_expression" OR NOT ir_output MATCHES "func @find_function" OR NOT ir_output MATCHES "func @name_exists" OR NOT ir_output MATCHES "func @parse_function" OR NOT ir_output MATCHES "func @parse_module" OR NOT ir_output MATCHES "func @build_hir" OR NOT ir_output MATCHES "func @lower_hir" OR NOT ir_output MATCHES "func @execute_mir_main")
  message(FATAL_ERROR "compiler candidate frontend Forge IR is missing lexer/parser/HIR/MIR entry points")
endif()
execute_process(
  COMMAND "${RAZ_EXE}" build "${project}" --profile debug --force
  RESULT_VARIABLE build_result OUTPUT_VARIABLE build_output ERROR_VARIABLE build_error)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "compiler candidate native lexer/parser/HIR/MIR build failed:\n${build_output}\n${build_error}")
endif()
if(WIN32)
  set(executable "${project}/target/debug/raz-compiler.exe")
else()
  set(executable "${project}/target/debug/raz-compiler")
endif()
if(NOT EXISTS "${executable}")
  message(FATAL_ERROR "compiler candidate lexer/parser/semantic executable missing: ${executable}")
endif()
file(WRITE "${project}/compiler-qualify.txt" "1")
execute_process(COMMAND "${executable}" "compiler-package.txt" "compiler-output.fir" RESULT_VARIABLE run_result WORKING_DIRECTORY "${project}")
if(NOT run_result EQUAL 0)
  message(FATAL_ERROR "compiler candidate lexer/parser/HIR/MIR qualification failed with ${run_result}")
endif()

set(cfg_ir "${project}/compiler-cfg-output.fir")
if(NOT EXISTS "${cfg_ir}")
  message(FATAL_ERROR "compiler candidate did not emit its CFG qualification Forge IR")
endif()
file(READ "${cfg_ir}" cfg_text)
string(FIND "${cfg_text}" "jump bb2(%r0)" cfg_jump_entry)
string(FIND "${cfg_text}" "bb2(%r2: i64):" cfg_loop_header)
string(FIND "${cfg_text}" "branch %r4, bb6(%r2), bb10(%r2)" cfg_branch)
string(FIND "${cfg_text}" "jump bb2(%r8)" cfg_backedge)
if(cfg_jump_entry EQUAL -1 OR cfg_loop_header EQUAL -1 OR cfg_branch EQUAL -1 OR cfg_backedge EQUAL -1)
  message(FATAL_ERROR "compiler candidate CFG serialization is missing block parameters or loop edges:
${cfg_text}")
endif()
if(WIN32)
  set(cfg_object "${project}/compiler-cfg-generated.obj")
  execute_process(COMMAND "${FORGE_CODEGEN}" "${cfg_ir}" "--emit-coff=${cfg_object}" "--abi=windows"
    RESULT_VARIABLE cfg_codegen_result OUTPUT_VARIABLE cfg_codegen_output ERROR_VARIABLE cfg_codegen_error)
else()
  set(cfg_object "${project}/compiler-cfg-generated.o")
  execute_process(COMMAND "${FORGE_CODEGEN}" "${cfg_ir}" "--emit-elf=${cfg_object}" "--abi=sysv"
    RESULT_VARIABLE cfg_codegen_result OUTPUT_VARIABLE cfg_codegen_output ERROR_VARIABLE cfg_codegen_error)
endif()
if(NOT cfg_codegen_result EQUAL 0 OR NOT EXISTS "${cfg_object}")
  message(FATAL_ERROR "compiler candidate CFG Forge object emission failed:
${cfg_codegen_output}
${cfg_codegen_error}")
endif()
if(NOT WIN32)
  set(cfg_executable "${project}/compiler-cfg-generated")
  execute_process(COMMAND cc "${cfg_object}" -o "${cfg_executable}"
    RESULT_VARIABLE cfg_link_result OUTPUT_VARIABLE cfg_link_output ERROR_VARIABLE cfg_link_error)
  if(NOT cfg_link_result EQUAL 0)
    message(FATAL_ERROR "compiler candidate CFG generated artifact link failed:
${cfg_link_output}
${cfg_link_error}")
  endif()
  execute_process(COMMAND "${cfg_executable}" RESULT_VARIABLE cfg_result)
  if(NOT cfg_result EQUAL 5)
    message(FATAL_ERROR "compiler candidate CFG generated artifact returned ${cfg_result}, expected 5")
  endif()
endif()

set(forge_ir "${project}/compiler-output.fir")
if(NOT EXISTS "${forge_ir}")
  message(FATAL_ERROR "compiler candidate did not emit Forge IR: ${forge_ir}")
endif()
file(READ "${forge_ir}" emitted_ir)
if(NOT emitted_ir MATCHES "module @raz_seed" OR NOT emitted_ir MATCHES "func @__raz_fn_identity" OR NOT emitted_ir MATCHES "func @__raz_fn_twice" OR NOT emitted_ir MATCHES "func @__raz_fn_adjust" OR NOT emitted_ir MATCHES "func @main" OR NOT emitted_ir MATCHES "func @__raz_fn_divide" OR NOT emitted_ir MATCHES "func @__raz_fn_combine" OR NOT emitted_ir MATCHES "func @__raz_fn_choose" OR NOT emitted_ir MATCHES "mul i64" OR NOT emitted_ir MATCHES "div.signed i64" OR NOT emitted_ir MATCHES "sub i64" OR NOT emitted_ir MATCHES "call i64 @__raz_fn_identity" OR NOT emitted_ir MATCHES "call i64 @__raz_fn_twice" OR NOT emitted_ir MATCHES "call i64 @__raz_fn_adjust" OR NOT emitted_ir MATCHES "call i64 @__raz_fn_divide" OR NOT emitted_ir MATCHES "call i64 @__raz_fn_combine" OR NOT emitted_ir MATCHES "call i64 @__raz_fn_choose" OR NOT emitted_ir MATCHES "cmp.ge i64" OR NOT emitted_ir MATCHES "select i64" OR NOT emitted_ir MATCHES "add i64" OR emitted_ir MATCHES "const i32 42")
  message(FATAL_ERROR "compiler candidate did not serialize the qualified MIR instruction set:
${emitted_ir}")
endif()
if(WIN32)
  set(object "${project}/compiler-generated.obj")
  execute_process(COMMAND "${FORGE_CODEGEN}" "${forge_ir}" "--emit-coff=${object}" "--abi=windows"
    RESULT_VARIABLE codegen_result OUTPUT_VARIABLE codegen_output ERROR_VARIABLE codegen_error)
else()
  set(object "${project}/compiler-generated.o")
  execute_process(COMMAND "${FORGE_CODEGEN}" "${forge_ir}" "--emit-elf=${object}" "--abi=sysv"
    RESULT_VARIABLE codegen_result OUTPUT_VARIABLE codegen_output ERROR_VARIABLE codegen_error)
endif()
if(NOT codegen_result EQUAL 0 OR NOT EXISTS "${object}")
  message(FATAL_ERROR "compiler candidate Forge object emission failed:
${codegen_output}
${codegen_error}")
endif()
if(NOT WIN32)
  set(native_executable "${project}/compiler-generated")
  execute_process(COMMAND c++ "${object}" "${RAZ_RUNTIME_LIB}" -pthread -lssl -lcrypto -o "${native_executable}"
    RESULT_VARIABLE link_result OUTPUT_VARIABLE link_output ERROR_VARIABLE link_error)
  if(NOT link_result EQUAL 0)
    message(FATAL_ERROR "compiler candidate generated artifact link failed:
${link_output}
${link_error}")
  endif()
  execute_process(COMMAND "${native_executable}" RESULT_VARIABLE generated_result)
  if(NOT generated_result EQUAL 42)
    message(FATAL_ERROR "compiler candidate generated native artifact returned ${generated_result}, expected 42")
  endif()
endif()

# The authoritative compiler candidate HIR frontend must reject a malformed assignment statement.
file(WRITE "${project}/compiler-invalid.rz" "public fn main() -> i64 { i64 value = 1; value = value + 1 return value; }\n")
file(WRITE "${project}/compiler-package.txt" "compiler-invalid.rz\n")
execute_process(COMMAND "${executable}" "compiler-package.txt" "compiler-invalid-output.fir" RESULT_VARIABLE bad_result WORKING_DIRECTORY "${project}")
if(NOT bad_result EQUAL 43)
  message(FATAL_ERROR "compiler candidate malformed statement package returned ${bad_result}, expected HIR frontend failure 43")
endif()
