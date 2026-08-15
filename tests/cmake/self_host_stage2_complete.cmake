# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED RAZ_EXE OR NOT DEFINED FORGE_CODEGEN OR NOT DEFINED RAZ_RUNTIME_LIB OR
   NOT DEFINED RAZ_FORGE_BRIDGE_LIB OR NOT DEFINED FORGE_LIB OR NOT DEFINED CXX_COMPILER OR
   NOT DEFINED SOURCE_ROOT OR NOT DEFINED WORK_ROOT)
  message(FATAL_ERROR "RAZ_EXE, FORGE_CODEGEN, RAZ_RUNTIME_LIB, RAZ_FORGE_BRIDGE_LIB, FORGE_LIB, CXX_COMPILER, SOURCE_ROOT, and WORK_ROOT are required")
endif()

if(NOT DEFINED RAZ_RUNTIME_LINK_MANIFEST OR NOT EXISTS "${RAZ_RUNTIME_LINK_MANIFEST}")
  message(FATAL_ERROR "Raz runtime link manifest is required: ${RAZ_RUNTIME_LINK_MANIFEST}")
endif()
file(STRINGS "${RAZ_RUNTIME_LINK_MANIFEST}" raz_runtime_link_deps)
list(FILTER raz_runtime_link_deps EXCLUDE REGEX "^[ \t]*$")

include("${SOURCE_ROOT}/tests/cmake/self_host_source_set.cmake")

file(REMOVE_RECURSE "${WORK_ROOT}")
file(MAKE_DIRECTORY "${WORK_ROOT}")

# Full compiler-sized Forge modules must use the optimized codegen helper.
# Debug Forge is intentionally assertion-heavy and can take many minutes here.
execute_process(
  COMMAND "${CMAKE_COMMAND}" --preset release
  WORKING_DIRECTORY "${SOURCE_ROOT}"
  RESULT_VARIABLE release_configure_result
  OUTPUT_VARIABLE release_configure_output
  ERROR_VARIABLE release_configure_error)
if(NOT release_configure_result EQUAL 0)
  message(FATAL_ERROR "Stage 2 qualification could not configure the optimized Forge toolchain:\n${release_configure_output}\n${release_configure_error}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" --build --preset release --target raz-selfhost-forge-codegen
  WORKING_DIRECTORY "${SOURCE_ROOT}"
  RESULT_VARIABLE release_build_result
  OUTPUT_VARIABLE release_build_output
  ERROR_VARIABLE release_build_error)
if(NOT release_build_result EQUAL 0)
  message(FATAL_ERROR "Stage 2 qualification could not build the optimized Forge codegen helper:\n${release_build_output}\n${release_build_error}")
endif()
if(WIN32)
  set(optimized_forge_codegen "${SOURCE_ROOT}/build/release/tests/raz-selfhost-forge-codegen.exe")
else()
  set(optimized_forge_codegen "${SOURCE_ROOT}/build/release/tests/raz-selfhost-forge-codegen")
endif()
if(NOT EXISTS "${optimized_forge_codegen}")
  message(FATAL_ERROR "Optimized Forge codegen helper is missing: ${optimized_forge_codegen}")
endif()
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
  message(FATAL_ERROR "Stage 2 qualification could not build Stage 1:\n${stage1_build_output}\n${stage1_build_error}")
endif()

if(WIN32)
  set(stage1_executable "${project}/target/host/debug/raz-compiler.exe")
  set(stage2_object "${project}/stage1-output.obj")
  set(stage2_executable "${project}/stage2.exe")
  set(smoke_object "${WORK_ROOT}/smoke/smoke.obj")
  set(smoke_executable "${WORK_ROOT}/smoke/smoke.exe")
  set(float_object "${WORK_ROOT}/float-smoke/float-smoke.obj")
  set(float_executable "${WORK_ROOT}/float-smoke/float-smoke.exe")
  set(width_object "${WORK_ROOT}/integer-width-smoke/integer-width-smoke.obj")
  set(width_executable "${WORK_ROOT}/integer-width-smoke/integer-width-smoke.exe")
  set(structured_scalar_object "${WORK_ROOT}/structured-scalar/structured-scalar.obj")
  set(structured_scalar_executable "${WORK_ROOT}/structured-scalar/structured-scalar.exe")
  set(structured_float_object "${WORK_ROOT}/structured-float/structured-float.obj")
  set(structured_float_executable "${WORK_ROOT}/structured-float/structured-float.exe")
  set(structured_aggregate_object "${WORK_ROOT}/structured-aggregate/structured-aggregate.obj")
  set(structured_aggregate_executable "${WORK_ROOT}/structured-aggregate/structured-aggregate.exe")
  set(structured_array_object "${WORK_ROOT}/structured-array/structured-array.obj")
  set(structured_array_executable "${WORK_ROOT}/structured-array/structured-array.exe")
  set(structured_slice_object "${WORK_ROOT}/structured-slice/structured-slice.obj")
  set(structured_slice_executable "${WORK_ROOT}/structured-slice/structured-slice.exe")
  set(structured_nested_object "${WORK_ROOT}/structured-nested/structured-nested.obj")
  set(structured_nested_executable "${WORK_ROOT}/structured-nested/structured-nested.exe")
else()
  set(stage1_executable "${project}/target/host/debug/raz-compiler")
  set(stage2_object "${project}/stage1-output.o")
  set(stage2_executable "${project}/stage2")
  set(smoke_object "${WORK_ROOT}/smoke/smoke.o")
  set(smoke_executable "${WORK_ROOT}/smoke/smoke")
  set(float_object "${WORK_ROOT}/float-smoke/float-smoke.o")
  set(float_executable "${WORK_ROOT}/float-smoke/float-smoke")
  set(width_object "${WORK_ROOT}/integer-width-smoke/integer-width-smoke.o")
  set(width_executable "${WORK_ROOT}/integer-width-smoke/integer-width-smoke")
  set(structured_scalar_object "${WORK_ROOT}/structured-scalar/structured-scalar.o")
  set(structured_scalar_executable "${WORK_ROOT}/structured-scalar/structured-scalar")
  set(structured_float_object "${WORK_ROOT}/structured-float/structured-float.o")
  set(structured_float_executable "${WORK_ROOT}/structured-float/structured-float")
  set(structured_aggregate_object "${WORK_ROOT}/structured-aggregate/structured-aggregate.o")
  set(structured_aggregate_executable "${WORK_ROOT}/structured-aggregate/structured-aggregate")
  set(structured_array_object "${WORK_ROOT}/structured-array/structured-array.o")
  set(structured_array_executable "${WORK_ROOT}/structured-array/structured-array")
  set(structured_slice_object "${WORK_ROOT}/structured-slice/structured-slice.o")
  set(structured_slice_executable "${WORK_ROOT}/structured-slice/structured-slice")
  set(structured_nested_object "${WORK_ROOT}/structured-nested/structured-nested.o")
  set(structured_nested_executable "${WORK_ROOT}/structured-nested/structured-nested")
endif()

file(COPY_FILE "${frontend}" "${project}/stage1-compiler.rz" ONLY_IF_DIFFERENT)
file(WRITE "${project}/stage1-package.txt" "stage1-compiler.rz\n")
execute_process(
  COMMAND "${stage1_executable}" --forge-native "stage1-package.txt" "stage1-output.fir"
  WORKING_DIRECTORY "${project}"
  RESULT_VARIABLE stage1_run_result)
if(NOT stage1_run_result EQUAL 0)
  message(FATAL_ERROR "Stage 1 compiler returned ${stage1_run_result} while emitting Stage 2")
endif()

set(stage2_ir "${project}/stage1-output.fir")
if(NOT EXISTS "${stage2_ir}")
  message(FATAL_ERROR "Stage 1 compiler did not emit Stage 2 Forge IR")
endif()
file(SIZE "${stage2_ir}" stage2_ir_size)
if(stage2_ir_size LESS 500000)
  message(FATAL_ERROR "Stage 2 Forge module is unexpectedly small: ${stage2_ir_size} bytes")
endif()

if(NOT EXISTS "${stage2_object}")
  message(FATAL_ERROR "Stage 1 in-process Forge backend did not emit native Stage 2 object")
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
  message(FATAL_ERROR "Native Stage 2 link failed:\n${stage2_link_output}\n${stage2_link_error}")
endif()

# Exercise the parser-free structured Forge C API path with real multi-function
# integer MIR: scalar globals/statics, stack locals, mutated parameters, loops,
# short-circuit control flow, block arguments, and direct calls.
# Link this object without raz_runtime: a successful link proves native output
# did not fall through the legacy FIR parser path, whose diagnostic FIR still
# contains the Stage 1 arena declarations/calls.
set(structured_scalar "${WORK_ROOT}/structured-scalar")
file(MAKE_DIRECTORY "${structured_scalar}")
file(WRITE "${structured_scalar}/main.rz" [=[global mut i64 counter = 4;
static i64 step = 2;

public fn bump_global() -> i64 {
    counter += step;
    return counter;
}

public fn both(i64 left, i64 right) -> i64 {
    if ((left > 0) && (right > 0)) {
        return 7;
    }
    return 2;
}

public fn sum_to(i64 limit) -> i64 {
    i64 index = 0;
    i64 total = 0;
    while (index < limit) {
        total += index;
        index += 1;
    }
    return total;
}

public fn bump(i64 value) -> i64 {
    value += 3;
    value *= 2;
    return value;
}

public fn main() -> i64 {
    return bump_global() + both(1, 1) + both(1, 0) + sum_to(5) + bump(1);
}
]=])
execute_process(
  COMMAND "${stage2_executable}" --forge-native --opt=3 "main.rz" "structured-scalar.fir"
  WORKING_DIRECTORY "${structured_scalar}"
  RESULT_VARIABLE structured_scalar_compile_result
  OUTPUT_VARIABLE structured_scalar_compile_output
  ERROR_VARIABLE structured_scalar_compile_error)
if(NOT structured_scalar_compile_result EQUAL 0 OR NOT EXISTS "${structured_scalar_object}")
  message(FATAL_ERROR "Structured Forge scalar compilation failed:\n${structured_scalar_compile_output}\n${structured_scalar_compile_error}")
endif()
execute_process(
  COMMAND "${CXX_COMPILER}" "${structured_scalar_object}" -o "${structured_scalar_executable}"
  RESULT_VARIABLE structured_scalar_link_result
  OUTPUT_VARIABLE structured_scalar_link_output
  ERROR_VARIABLE structured_scalar_link_error)
if(NOT structured_scalar_link_result EQUAL 0 OR NOT EXISTS "${structured_scalar_executable}")
  message(FATAL_ERROR "Structured Forge scalar object required an unexpected runtime dependency:\n${structured_scalar_link_output}\n${structured_scalar_link_error}")
endif()
execute_process(
  COMMAND "${structured_scalar_executable}"
  RESULT_VARIABLE structured_scalar_run_result)
if(NOT structured_scalar_run_result EQUAL 33)
  message(FATAL_ERROR "Structured Forge scalar smoke returned ${structured_scalar_run_result}, expected 33")
endif()

# Exercise the same parser-free structured path with floating-point values and
# a real external C-ABI declaration. Linking without raz_runtime keeps the same
# invariant as the integer smoke: any fallback to the diagnostic FIR path would
# introduce Stage 1 arena symbols and fail this link. `fabs` is supplied by the
# platform C runtime, so this also qualifies @abi(C) + @link_name for f64.
set(structured_float "${WORK_ROOT}/structured-float")
file(MAKE_DIRECTORY "${structured_float}")
file(WRITE "${structured_float}/main.rz" [=[@abi(C) @link_name(fabs) extern fn native_abs(f64 value) -> f64;

fn blend(f64 left, f64 right) -> f64 {
    return left * right + 0.625;
}

public fn main() -> i64 {
    f64 internal = blend(1.5, 2.25);
    i64 integer = 41;
    f64 converted = integer as f64;
    i64 roundtrip = converted as i64;
    f32 small = 3.5 as f32;
    f64 widened = small as f64;
    f64 absolute = native_abs(-6.0);
    if (internal > 3.99 && internal < 4.01 && roundtrip == 41 && widened > 3.49 && widened < 3.51 && absolute > 5.99 && absolute < 6.01) {
        return 42;
    }
    return 1;
}
]=])
execute_process(
  COMMAND "${stage2_executable}" --forge-native --opt=3 "main.rz" "structured-float.fir"
  WORKING_DIRECTORY "${structured_float}"
  RESULT_VARIABLE structured_float_compile_result
  OUTPUT_VARIABLE structured_float_compile_output
  ERROR_VARIABLE structured_float_compile_error)
if(NOT structured_float_compile_result EQUAL 0 OR NOT EXISTS "${structured_float_object}")
  message(FATAL_ERROR "Structured Forge float/C-ABI compilation failed:\n${structured_float_compile_output}\n${structured_float_compile_error}")
endif()
if(WIN32)
  execute_process(
    COMMAND "${CXX_COMPILER}" "${structured_float_object}" -o "${structured_float_executable}"
    RESULT_VARIABLE structured_float_link_result
    OUTPUT_VARIABLE structured_float_link_output
    ERROR_VARIABLE structured_float_link_error)
else()
  execute_process(
    COMMAND "${CXX_COMPILER}" "${structured_float_object}" -lm -o "${structured_float_executable}"
    RESULT_VARIABLE structured_float_link_result
    OUTPUT_VARIABLE structured_float_link_output
    ERROR_VARIABLE structured_float_link_error)
endif()
if(NOT structured_float_link_result EQUAL 0 OR NOT EXISTS "${structured_float_executable}")
  message(FATAL_ERROR "Structured Forge float/C-ABI object required an unexpected Raz runtime dependency:\n${structured_float_link_output}\n${structured_float_link_error}")
endif()
execute_process(
  COMMAND "${structured_float_executable}"
  RESULT_VARIABLE structured_float_run_result)
if(NOT structured_float_run_result EQUAL 42)
  message(FATAL_ERROR "Structured Forge float/C-ABI smoke returned ${structured_float_run_result}, expected 42")
endif()

# Exercise true Forge aggregate ABI metadata rather than the legacy pointer-only
# convention. The structured-only flag forbids FIR fallback; linking without the
# Raz runtime proves the result is a self-contained native aggregate path.
set(structured_aggregate "${WORK_ROOT}/structured-aggregate")
file(MAKE_DIRECTORY "${structured_aggregate}")
file(WRITE "${structured_aggregate}/main.rz" [=[struct Pair {
    i64 left;
    i64 right;
}

fn make_pair(i64 left, i64 right) -> Pair {
    return Pair(left, right);
}

fn sum_pair(Pair pair) -> i64 {
    return pair.left + pair.right;
}

public fn main() -> i64 {
    Pair pair = make_pair(19, 23);
    return sum_pair(pair);
}
]=])
execute_process(
  COMMAND "${stage2_executable}" --forge-native --forge-structured-only --opt=3 "main.rz" "structured-aggregate.fir"
  WORKING_DIRECTORY "${structured_aggregate}"
  RESULT_VARIABLE structured_aggregate_compile_result
  OUTPUT_VARIABLE structured_aggregate_compile_output
  ERROR_VARIABLE structured_aggregate_compile_error)
if(NOT structured_aggregate_compile_result EQUAL 0 OR NOT EXISTS "${structured_aggregate_object}")
  message(FATAL_ERROR "Structured Forge aggregate ABI compilation failed:\n${structured_aggregate_compile_output}\n${structured_aggregate_compile_error}")
endif()
execute_process(
  COMMAND "${CXX_COMPILER}" "${structured_aggregate_object}" -o "${structured_aggregate_executable}"
  RESULT_VARIABLE structured_aggregate_link_result
  OUTPUT_VARIABLE structured_aggregate_link_output
  ERROR_VARIABLE structured_aggregate_link_error)
if(NOT structured_aggregate_link_result EQUAL 0 OR NOT EXISTS "${structured_aggregate_executable}")
  message(FATAL_ERROR "Structured Forge aggregate object required an unexpected runtime dependency:\n${structured_aggregate_link_output}\n${structured_aggregate_link_error}")
endif()
execute_process(
  COMMAND "${structured_aggregate_executable}"
  RESULT_VARIABLE structured_aggregate_run_result)
if(NOT structured_aggregate_run_result EQUAL 42)
  message(FATAL_ERROR "Structured Forge aggregate ABI smoke returned ${structured_aggregate_run_result}, expected 42")
endif()

# Exercise fixed-array by-value ABI semantics. The callee must receive an
# isolated copy: it mutates element 0 to produce 42 while the caller's original
# array remains unchanged. Structured-only plus no Raz runtime link prevents a
# hidden arena/FIR fallback from satisfying this regression.
set(structured_array "${WORK_ROOT}/structured-array")
file(MAKE_DIRECTORY "${structured_array}")
file(WRITE "${structured_array}/main.rz" [=[fn mutate_copy(i64 values[4]) -> i64 {
    values[0] = 40;
    return values[0] + values[1];
}

public fn main() -> i64 {
    i64 values[4] = [1, 2, 3, 4];
    i64 result = mutate_copy(values);
    if (values[0] != 1) {
        return 1;
    }
    return result;
}
]=])
execute_process(
  COMMAND "${stage2_executable}" --forge-native --forge-structured-only --opt=3 "main.rz" "structured-array.fir"
  WORKING_DIRECTORY "${structured_array}"
  RESULT_VARIABLE structured_array_compile_result
  OUTPUT_VARIABLE structured_array_compile_output
  ERROR_VARIABLE structured_array_compile_error)
if(NOT structured_array_compile_result EQUAL 0 OR NOT EXISTS "${structured_array_object}")
  message(FATAL_ERROR "Structured Forge fixed-array ABI compilation failed:\n${structured_array_compile_output}\n${structured_array_compile_error}")
endif()
execute_process(
  COMMAND "${CXX_COMPILER}" "${structured_array_object}" -o "${structured_array_executable}"
  RESULT_VARIABLE structured_array_link_result
  OUTPUT_VARIABLE structured_array_link_output
  ERROR_VARIABLE structured_array_link_error)
if(NOT structured_array_link_result EQUAL 0 OR NOT EXISTS "${structured_array_executable}")
  message(FATAL_ERROR "Structured Forge fixed-array object required an unexpected runtime dependency:\n${structured_array_link_output}\n${structured_array_link_error}")
endif()
execute_process(
  COMMAND "${structured_array_executable}"
  RESULT_VARIABLE structured_array_run_result)
if(NOT structured_array_run_result EQUAL 42)
  message(FATAL_ERROR "Structured Forge fixed-array ABI smoke returned ${structured_array_run_result}, expected 42")
endif()

# Slices have the native two-field layout {data pointer, length}. Require that
# layout to cross direct calls and return by value on the parser-free path.
# Mutation through []mut must still target the caller's backing array while the
# slice descriptor itself is passed by value.
set(structured_slice "${WORK_ROOT}/structured-slice")
file(MAKE_DIRECTORY "${structured_slice}")
file(WRITE "${structured_slice}/main.rz" [=[fn identity(i64[] values) -> i64[] {
    return values;
}

fn sum(i64[] values) -> i64 {
    return values[0] + values[1] + values.length;
}

fn bump(i64[]mut values) {
    values[0] += 30;
}

public fn main() -> i64 {
    i64 values[2] = [5, 5];
    i64[] view = identity(&values);
    i64 before = sum(view);
    i64[]mut edit = &mut values;
    bump(edit);
    return before + values[0] - 5;
}
]=])
execute_process(
  COMMAND "${stage2_executable}" --forge-native --forge-structured-only --opt=3 "main.rz" "structured-slice.fir"
  WORKING_DIRECTORY "${structured_slice}"
  RESULT_VARIABLE structured_slice_compile_result
  OUTPUT_VARIABLE structured_slice_compile_output
  ERROR_VARIABLE structured_slice_compile_error)
if(NOT structured_slice_compile_result EQUAL 0 OR NOT EXISTS "${structured_slice_object}")
  message(FATAL_ERROR "Structured Forge slice ABI compilation failed:\n${structured_slice_compile_output}\n${structured_slice_compile_error}")
endif()
execute_process(
  COMMAND "${CXX_COMPILER}" "${structured_slice_object}" -o "${structured_slice_executable}"
  RESULT_VARIABLE structured_slice_link_result
  OUTPUT_VARIABLE structured_slice_link_output
  ERROR_VARIABLE structured_slice_link_error)
if(NOT structured_slice_link_result EQUAL 0 OR NOT EXISTS "${structured_slice_executable}")
  message(FATAL_ERROR "Structured Forge slice object required an unexpected runtime dependency:\n${structured_slice_link_output}\n${structured_slice_link_error}")
endif()
execute_process(
  COMMAND "${structured_slice_executable}"
  RESULT_VARIABLE structured_slice_run_result)
if(NOT structured_slice_run_result EQUAL 42)
  message(FATAL_ERROR "Structured Forge slice ABI smoke returned ${structured_slice_run_result}, expected 42")
endif()

# Nested native-layout structs should remain real aggregates all the way through
# the structured Forge path. This exercises a nested struct plus an embedded
# native-width array and slice descriptor without linking raz_runtime.
set(structured_nested "${WORK_ROOT}/structured-nested")
file(MAKE_DIRECTORY "${structured_nested}")
file(WRITE "${structured_nested}/main.rz" [=[struct Inner {
    i64 left;
    i64 right;
}

struct Outer {
    Inner pair;
    i64 values[2];
}

fn sum_outer(Outer value) -> i64 {
    return value.pair.left + value.pair.right + value.values[0] + value.values[1];
}

public fn main() -> i64 {
    Outer value;
    value.pair.left = 10;
    value.pair.right = 20;
    value.values[0] = 5;
    value.values[1] = 7;
    return sum_outer(value);
}
]=])
execute_process(
  COMMAND "${stage2_executable}" --forge-native --forge-structured-only --opt=3 "main.rz" "structured-nested.fir"
  WORKING_DIRECTORY "${structured_nested}"
  RESULT_VARIABLE structured_nested_compile_result
  OUTPUT_VARIABLE structured_nested_compile_output
  ERROR_VARIABLE structured_nested_compile_error)
if(NOT structured_nested_compile_result EQUAL 0 OR NOT EXISTS "${structured_nested_object}")
  message(FATAL_ERROR "Structured Forge nested aggregate compilation failed:\n${structured_nested_compile_output}\n${structured_nested_compile_error}")
endif()
execute_process(
  COMMAND "${CXX_COMPILER}" "${structured_nested_object}" -o "${structured_nested_executable}"
  RESULT_VARIABLE structured_nested_link_result
  OUTPUT_VARIABLE structured_nested_link_output
  ERROR_VARIABLE structured_nested_link_error)
if(NOT structured_nested_link_result EQUAL 0 OR NOT EXISTS "${structured_nested_executable}")
  message(FATAL_ERROR "Structured Forge nested aggregate object required an unexpected runtime dependency:\n${structured_nested_link_output}\n${structured_nested_link_error}")
endif()
execute_process(
  COMMAND "${structured_nested_executable}"
  RESULT_VARIABLE structured_nested_run_result)
if(NOT structured_nested_run_result EQUAL 42)
  message(FATAL_ERROR "Structured Forge nested aggregate smoke returned ${structured_nested_run_result}, expected 42")
endif()

# Dynamic aggregate-element array indexing must stay on the structured path.
set(structured_dynamic_array "${WORK_ROOT}/structured-dynamic-array")
file(MAKE_DIRECTORY "${structured_dynamic_array}")
file(WRITE "${structured_dynamic_array}/main.rz" [=[struct Pair {
    i64 left;
    i64 right;
}

fn read_left(Pair[2] values, i64 index) -> i64 {
    return values[index].left;
}

public fn main() -> i64 {
    Pair[2] values;
    values[0].left = 10;
    values[0].right = 20;
    values[1].left = 42;
    values[1].right = 7;
    i64 index = 1;
    return read_left(values, index);
}
]=])
set(structured_dynamic_array_object "${structured_dynamic_array}/structured-dynamic-array${OBJECT_SUFFIX}")
set(structured_dynamic_array_executable "${structured_dynamic_array}/structured-dynamic-array${EXECUTABLE_SUFFIX}")
execute_process(
  COMMAND "${stage2_executable}" --forge-native --forge-structured-only --opt=3 "main.rz" "structured-dynamic-array.fir"
  WORKING_DIRECTORY "${structured_dynamic_array}"
  RESULT_VARIABLE structured_dynamic_array_compile_result
  OUTPUT_VARIABLE structured_dynamic_array_compile_output
  ERROR_VARIABLE structured_dynamic_array_compile_error)
if(NOT structured_dynamic_array_compile_result EQUAL 0 OR NOT EXISTS "${structured_dynamic_array_object}")
  message(FATAL_ERROR "Structured Forge dynamic aggregate-array compilation failed:\n${structured_dynamic_array_compile_output}\n${structured_dynamic_array_compile_error}")
endif()
execute_process(
  COMMAND "${CXX_COMPILER}" "${structured_dynamic_array_object}" -o "${structured_dynamic_array_executable}"
  RESULT_VARIABLE structured_dynamic_array_link_result
  OUTPUT_VARIABLE structured_dynamic_array_link_output
  ERROR_VARIABLE structured_dynamic_array_link_error)
if(NOT structured_dynamic_array_link_result EQUAL 0 OR NOT EXISTS "${structured_dynamic_array_executable}")
  message(FATAL_ERROR "Structured Forge dynamic aggregate-array object required an unexpected runtime dependency:\n${structured_dynamic_array_link_output}\n${structured_dynamic_array_link_error}")
endif()
execute_process(COMMAND "${structured_dynamic_array_executable}" RESULT_VARIABLE structured_dynamic_array_run_result)
if(NOT structured_dynamic_array_run_result EQUAL 42)
  message(FATAL_ERROR "Structured Forge dynamic aggregate-array smoke returned ${structured_dynamic_array_run_result}, expected 42")
endif()

# Escaping nested aggregates must be returned by value. The callee constructs
# stack-backed nested images, but the ABI return must copy the complete native
# aggregate out before that stack frame disappears.
set(structured_escape "${WORK_ROOT}/structured-escape")
file(MAKE_DIRECTORY "${structured_escape}")
file(WRITE "${structured_escape}/main.rz" [=[struct Pair {
    i64 left;
    i64 right;
}

struct Bundle {
    Pair pair;
    Pair[2] items;
}

fn make_bundle() -> Bundle {
    Bundle value;
    value.pair.left = 5;
    value.pair.right = 7;
    value.items[0].left = 10;
    value.items[1].left = 20;
    return value;
}

public fn main() -> i64 {
    Bundle value = make_bundle();
    return value.pair.left + value.pair.right + value.items[0].left + value.items[1].left;
}
]=])
set(structured_escape_object "${structured_escape}/structured-escape${OBJECT_SUFFIX}")
set(structured_escape_executable "${structured_escape}/structured-escape${EXECUTABLE_SUFFIX}")
execute_process(
  COMMAND "${stage2_executable}" --forge-native --forge-structured-only --opt=3 "main.rz" "structured-escape.fir"
  WORKING_DIRECTORY "${structured_escape}"
  RESULT_VARIABLE structured_escape_compile_result
  OUTPUT_VARIABLE structured_escape_compile_output
  ERROR_VARIABLE structured_escape_compile_error)
if(NOT structured_escape_compile_result EQUAL 0 OR NOT EXISTS "${structured_escape_object}")
  message(FATAL_ERROR "Structured Forge escaping aggregate compilation failed:\n${structured_escape_compile_output}\n${structured_escape_compile_error}")
endif()
execute_process(
  COMMAND "${CXX_COMPILER}" "${structured_escape_object}" -o "${structured_escape_executable}"
  RESULT_VARIABLE structured_escape_link_result
  OUTPUT_VARIABLE structured_escape_link_output
  ERROR_VARIABLE structured_escape_link_error)
if(NOT structured_escape_link_result EQUAL 0 OR NOT EXISTS "${structured_escape_executable}")
  message(FATAL_ERROR "Structured Forge escaping aggregate object required an unexpected runtime dependency:\n${structured_escape_link_output}\n${structured_escape_link_error}")
endif()
execute_process(COMMAND "${structured_escape_executable}" RESULT_VARIABLE structured_escape_run_result)
if(NOT structured_escape_run_result EQUAL 42)
  message(FATAL_ERROR "Structured Forge escaping aggregate smoke returned ${structured_escape_run_result}, expected 42")
endif()

# Structured Forge TLS must be genuine native TLS, not a process-global fallback.
set(structured_tls "${WORK_ROOT}/structured-tls")
file(MAKE_DIRECTORY "${structured_tls}")
file(WRITE "${structured_tls}/main.rz" [=[thread_local global mut i64 tls_counter = 40;

fn bump_tls() -> i64 {
    tls_counter += 2;
    return tls_counter;
}

public fn main() -> i64 {
    return bump_tls();
}
]=])
set(structured_tls_object "${structured_tls}/structured-tls${OBJECT_SUFFIX}")
set(structured_tls_executable "${structured_tls}/structured-tls${EXECUTABLE_SUFFIX}")
execute_process(
  COMMAND "${stage2_executable}" --forge-native --forge-structured-only --opt=3 "main.rz" "structured-tls.fir"
  WORKING_DIRECTORY "${structured_tls}"
  RESULT_VARIABLE structured_tls_compile_result
  OUTPUT_VARIABLE structured_tls_compile_output
  ERROR_VARIABLE structured_tls_compile_error)
if(NOT structured_tls_compile_result EQUAL 0 OR NOT EXISTS "${structured_tls_object}")
  message(FATAL_ERROR "Structured Forge TLS compilation failed:\n${structured_tls_compile_output}\n${structured_tls_compile_error}")
endif()
execute_process(
  COMMAND "${CXX_COMPILER}" "${structured_tls_object}" -o "${structured_tls_executable}"
  RESULT_VARIABLE structured_tls_link_result
  OUTPUT_VARIABLE structured_tls_link_output
  ERROR_VARIABLE structured_tls_link_error)
if(NOT structured_tls_link_result EQUAL 0 OR NOT EXISTS "${structured_tls_executable}")
  message(FATAL_ERROR "Structured Forge TLS object failed to link without raz_runtime:\n${structured_tls_link_output}\n${structured_tls_link_error}")
endif()
execute_process(COMMAND "${structured_tls_executable}" RESULT_VARIABLE structured_tls_run_result)
if(NOT structured_tls_run_result EQUAL 42)
  message(FATAL_ERROR "Structured Forge TLS smoke returned ${structured_tls_run_result}, expected 42")
endif()

set(smoke "${WORK_ROOT}/smoke")
file(MAKE_DIRECTORY "${smoke}")
file(WRITE "${smoke}/main.rz" [=[/* Scalar parity: nested /* block */ comments, bases, bitwise, shifts, and compounds. */
public fn main() -> i64 {
    const i64 local_bias = 2;
    i64 value = 0x2A;
    value += 8;
    value *= 3;
    value /= 5;
    value %= 7;
    value |= 8;
    value ^= 3;
    value &= 15;
    value <<= 2;
    value >>= 1;

    i64 bases = 0b1010 + 0o7 + 0xF + 1_000;
    i64 signed_div = -20 / 6;
    i64 signed_rem = -20 % 6;
    i64 precedence = 1 | 2 & 4 ^ 8;
    i64 shift = 1 + 2 << 3;
    i64 complement = ~0;

    i64 loop_total = 0;
    i64 outer = 0;
    while (outer < 4) {
        outer += 1;
        if (outer == 2) {
            continue;
        }
        i64 inner = 0;
        while (inner < 5) {
            inner += 1;
            if (inner == 3) {
                break;
            }
            loop_total += outer;
        }
    }

    i64 breaker = 0;
    while (breaker < 10) {
        breaker += 1;
        if (breaker == 5) {
            break;
        }
    }

    if (value == 18 && bases == 1032 && signed_div == -3 && signed_rem == -2 && precedence == 9 && shift == 24 && complement == -1 && loop_total == 16 && breaker == 5 && local_bias == 2) {
        return 42;
    } else {
        return 1;
    }
}
]=])
file(WRITE "${smoke}/stage1-package.txt" "main.rz\n")
execute_process(
  COMMAND "${stage2_executable}"
  WORKING_DIRECTORY "${smoke}"
  RESULT_VARIABLE stage2_smoke_result)
if(NOT stage2_smoke_result EQUAL 0)
  message(FATAL_ERROR "Native Stage 2 compiler returned ${stage2_smoke_result} for the smoke package")
endif()

set(smoke_ir "${smoke}/stage1-output.fir")
if(NOT EXISTS "${smoke_ir}")
  message(FATAL_ERROR "Native Stage 2 compiler did not emit smoke Forge IR")
endif()

# The self-hosted compiler also supports explicit manifest/output paths. Keep
# legacy no-argument mode above for bootstrap compatibility, then require the
# CLI path to produce exactly the same module.
set(cli_smoke_ir "${smoke}/explicit-output.fir")
execute_process(
  COMMAND "${stage2_executable}" "stage1-package.txt" "explicit-output.fir"
  WORKING_DIRECTORY "${smoke}"
  RESULT_VARIABLE stage2_cli_result)
if(NOT stage2_cli_result EQUAL 0 OR NOT EXISTS "${cli_smoke_ir}")
  message(FATAL_ERROR "Native Stage 2 explicit-path CLI failed with ${stage2_cli_result}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E compare_files "${smoke_ir}" "${cli_smoke_ir}"
  RESULT_VARIABLE stage2_cli_compare)
if(NOT stage2_cli_compare EQUAL 0)
  message(FATAL_ERROR "Legacy and explicit-manifest Stage 2 outputs differ")
endif()

# A normal .rz file is also a first-class self-hosted compiler input. Direct
# source mode must be deterministic with the one-file manifest path.
set(direct_smoke_ir "${smoke}/direct-output.fir")
execute_process(
  COMMAND "${stage2_executable}" "main.rz" "direct-output.fir"
  WORKING_DIRECTORY "${smoke}"
  RESULT_VARIABLE stage2_direct_result)
if(NOT stage2_direct_result EQUAL 0 OR NOT EXISTS "${direct_smoke_ir}")
  message(FATAL_ERROR "Native Stage 2 direct-source CLI failed with ${stage2_direct_result}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E compare_files "${smoke_ir}" "${direct_smoke_ir}"
  RESULT_VARIABLE stage2_direct_compare)
if(NOT stage2_direct_compare EQUAL 0)
  message(FATAL_ERROR "Manifest and direct-source Stage 2 outputs differ")
endif()

# break/continue are legal only inside loops. Native Stage 2 must reject both
# forms at function scope rather than emitting an invalid jump target.
file(WRITE "${smoke}/invalid-break.rz" [=[public fn main() -> i64 {
    break;
    return 0;
}
]=])
execute_process(
  COMMAND "${stage2_executable}" "invalid-break.rz" "invalid-break.fir"
  WORKING_DIRECTORY "${smoke}"
  RESULT_VARIABLE invalid_break_result)
if(invalid_break_result EQUAL 0 OR EXISTS "${smoke}/invalid-break.fir")
  message(FATAL_ERROR "Native Stage 2 accepted break outside a loop")
endif()

file(WRITE "${smoke}/invalid-continue.rz" [=[public fn main() -> i64 {
    continue;
    return 0;
}
]=])
execute_process(
  COMMAND "${stage2_executable}" "invalid-continue.rz" "invalid-continue.fir"
  WORKING_DIRECTORY "${smoke}"
  RESULT_VARIABLE invalid_continue_result)
if(invalid_continue_result EQUAL 0 OR EXISTS "${smoke}/invalid-continue.fir")
  message(FATAL_ERROR "Native Stage 2 accepted continue outside a loop")
endif()

# Mixed i64/f64 arithmetic requires an explicit cast in the self-hosted compiler.
file(WRITE "${smoke}/invalid-mixed-numeric.rz" [=[public fn main() -> i64 {
    i64 integer = 1;
    f64 floating = 2.0;
    f64 result = integer + floating;
    return 0;
}
]=])
execute_process(
  COMMAND "${stage2_executable}" "invalid-mixed-numeric.rz" "invalid-mixed-numeric.fir"
  WORKING_DIRECTORY "${smoke}"
  RESULT_VARIABLE invalid_mixed_numeric_result)
if(invalid_mixed_numeric_result EQUAL 0 OR EXISTS "${smoke}/invalid-mixed-numeric.fir")
  message(FATAL_ERROR "Native Stage 2 accepted mixed numeric arithmetic without an explicit cast")
endif()

file(READ "${smoke_ir}" smoke_ir_text)
if(NOT smoke_ir_text MATCHES "func @main\\(\\) -> i64" OR
   NOT smoke_ir_text MATCHES "const i64 42" OR
   NOT smoke_ir_text MATCHES "func @_raz_entry\\(\\) -> i32")
  message(FATAL_ERROR "Native Stage 2 smoke Forge IR is incomplete:\n${smoke_ir_text}")
endif()
foreach(required_opcode IN ITEMS "div.signed" "rem.signed" "and" "or" "xor" "shl" "shr.signed")
  if(NOT smoke_ir_text MATCHES "${required_opcode}")
    message(FATAL_ERROR "Scalar parity Forge IR is missing '${required_opcode}'")
  endif()
endforeach()

if(WIN32)
  execute_process(
    COMMAND "${optimized_forge_codegen}" "${smoke_ir}" "--emit-coff=${smoke_object}" --abi=windows
    RESULT_VARIABLE smoke_codegen_result
    OUTPUT_VARIABLE smoke_codegen_output
    ERROR_VARIABLE smoke_codegen_error)
else()
  execute_process(
    COMMAND "${optimized_forge_codegen}" "${smoke_ir}" "--emit-elf=${smoke_object}" --abi=sysv
    RESULT_VARIABLE smoke_codegen_result
    OUTPUT_VARIABLE smoke_codegen_output
    ERROR_VARIABLE smoke_codegen_error)
endif()
if(NOT smoke_codegen_result EQUAL 0 OR NOT EXISTS "${smoke_object}")
  message(FATAL_ERROR "Smoke native object emission failed:\n${smoke_codegen_output}\n${smoke_codegen_error}")
endif()

if(WIN32)
  execute_process(
    COMMAND "${CXX_COMPILER}" "${smoke_object}" "${RAZ_RUNTIME_LIB}" ${raz_runtime_link_deps} -o "${smoke_executable}"
    RESULT_VARIABLE smoke_link_result
    OUTPUT_VARIABLE smoke_link_output
    ERROR_VARIABLE smoke_link_error)
else()
  execute_process(
    COMMAND "${CXX_COMPILER}" "${smoke_object}" "${RAZ_RUNTIME_LIB}" ${raz_runtime_link_deps} -pthread -o "${smoke_executable}"
    RESULT_VARIABLE smoke_link_result
    OUTPUT_VARIABLE smoke_link_output
    ERROR_VARIABLE smoke_link_error)
endif()
if(NOT smoke_link_result EQUAL 0 OR NOT EXISTS "${smoke_executable}")
  message(FATAL_ERROR "Smoke native link failed:\n${smoke_link_output}\n${smoke_link_error}")
endif()

execute_process(COMMAND "${smoke_executable}" RESULT_VARIABLE smoke_result)
if(NOT smoke_result EQUAL 42)
  message(FATAL_ERROR "Native smoke executable returned ${smoke_result}, expected 42")
endif()

# Aggregate parity: named structs plus canonical tuple types/literals/projections must survive
# the Raz-written frontend, HIR/MIR, Forge emission, and native execution.
set(struct_constructor_smoke "${WORK_ROOT}/struct-constructor-smoke")
file(MAKE_DIRECTORY "${struct_constructor_smoke}")
file(WRITE "${struct_constructor_smoke}/main.rz" [=[struct Pair {
    i64 left;
    i64 right;
}

fn make_pair(i64 left, i64 right) -> Pair {
    return Pair(left, right);
}

fn swap((i64,bool) input) -> (bool,i64) {
    return (input.1, input.0);
}

public fn main() -> i64 {
    Pair pair = make_pair(20, 15);
    (i64,bool) tuple = (7, true);
    (bool,i64) reversed = swap(tuple);
    if reversed.0 {
        return pair.left + pair.right + reversed.1;
    }
    return 1;
}
]=])
file(WRITE "${struct_constructor_smoke}/stage1-package.txt" "main.rz\n")
execute_process(
  COMMAND "${stage2_executable}" "stage1-package.txt" "struct.fir"
  WORKING_DIRECTORY "${struct_constructor_smoke}"
  RESULT_VARIABLE struct_compile_result)
if(NOT struct_compile_result EQUAL 0)
  message(FATAL_ERROR "Native Stage 2 rejected named struct construction with ${struct_compile_result}")
endif()
set(struct_ir "${struct_constructor_smoke}/struct.fir")
if(WIN32)
  set(struct_object "${struct_constructor_smoke}/struct.obj")
  set(struct_executable "${struct_constructor_smoke}/struct.exe")
  execute_process(
    COMMAND "${optimized_forge_codegen}" "${struct_ir}" "--emit-coff=${struct_object}" --abi=windows
    RESULT_VARIABLE struct_codegen_result
    OUTPUT_VARIABLE struct_codegen_output
    ERROR_VARIABLE struct_codegen_error)
else()
  set(struct_object "${struct_constructor_smoke}/struct.o")
  set(struct_executable "${struct_constructor_smoke}/struct")
  execute_process(
    COMMAND "${optimized_forge_codegen}" "${struct_ir}" "--emit-elf=${struct_object}" --abi=sysv
    RESULT_VARIABLE struct_codegen_result
    OUTPUT_VARIABLE struct_codegen_output
    ERROR_VARIABLE struct_codegen_error)
endif()
if(NOT struct_codegen_result EQUAL 0 OR NOT EXISTS "${struct_object}")
  message(FATAL_ERROR "Struct constructor object emission failed:\n${struct_codegen_output}\n${struct_codegen_error}")
endif()
if(WIN32)
  execute_process(
    COMMAND "${CXX_COMPILER}" "${struct_object}" "${RAZ_RUNTIME_LIB}" ${raz_runtime_link_deps} -o "${struct_executable}"
    RESULT_VARIABLE struct_link_result
    OUTPUT_VARIABLE struct_link_output
    ERROR_VARIABLE struct_link_error)
else()
  execute_process(
    COMMAND "${CXX_COMPILER}" "${struct_object}" "${RAZ_RUNTIME_LIB}" ${raz_runtime_link_deps} -pthread -o "${struct_executable}"
    RESULT_VARIABLE struct_link_result
    OUTPUT_VARIABLE struct_link_output
    ERROR_VARIABLE struct_link_error)
endif()
if(NOT struct_link_result EQUAL 0 OR NOT EXISTS "${struct_executable}")
  message(FATAL_ERROR "Struct constructor link failed:\n${struct_link_output}\n${struct_link_error}")
endif()
execute_process(COMMAND "${struct_executable}" RESULT_VARIABLE struct_result)
if(NOT struct_result EQUAL 42)
  message(FATAL_ERROR "Struct constructor executable returned ${struct_result}, expected 42")
endif()

# Structural/control-flow parity: integer ranges, exhaustive unit-enum match,
# explicit enum discriminants, unsafe lexical blocks, and character literals.
set(structural_smoke "${WORK_ROOT}/structural-smoke")
file(MAKE_DIRECTORY "${structural_smoke}")
file(WRITE "${structural_smoke}/main.rz" [=[enum Status {
    Ready,
    Running = 4,
    Stopped,
}

fn code(Status status) -> i64 {
    match status {
        Status::Ready => { return 10; },
        Status::Running => { return 20; },
        Status::Stopped => { return 30; },
    }
}

public fn main() -> i64 {
    i64 total = 0;
    for value in 1..=5 {
        if (value == 3) {
            continue;
        }
        total += value;
    }
    for value in 0..10 {
        if (value == 5) {
            break;
        }
        total += 1;
    }
    Status status = Status::Running;
    unsafe {
        total += code(status);
    }
    char marker = '*';
    char newline = '\n';
    if (total == 37 && marker == 42 && newline == 10) {
        return 42;
    }
    return 1;
}
]=])
set(structural_ir "${structural_smoke}/structural-output.fir")
execute_process(
  COMMAND "${stage2_executable}" "main.rz" "structural-output.fir"
  WORKING_DIRECTORY "${structural_smoke}"
  RESULT_VARIABLE structural_compile_result)
if(NOT structural_compile_result EQUAL 0 OR NOT EXISTS "${structural_ir}")
  message(FATAL_ERROR "Native Stage 2 structural compilation failed with ${structural_compile_result}")
endif()
if(WIN32)
  set(structural_object "${structural_smoke}/structural-smoke.obj")
  set(structural_executable "${structural_smoke}/structural-smoke.exe")
  execute_process(COMMAND "${optimized_forge_codegen}" "${structural_ir}" "--emit-coff=${structural_object}" --abi=windows RESULT_VARIABLE structural_codegen_result OUTPUT_VARIABLE structural_codegen_output ERROR_VARIABLE structural_codegen_error)
else()
  set(structural_object "${structural_smoke}/structural-smoke.o")
  set(structural_executable "${structural_smoke}/structural-smoke")
  execute_process(COMMAND "${optimized_forge_codegen}" "${structural_ir}" "--emit-elf=${structural_object}" --abi=sysv RESULT_VARIABLE structural_codegen_result OUTPUT_VARIABLE structural_codegen_output ERROR_VARIABLE structural_codegen_error)
endif()
if(NOT structural_codegen_result EQUAL 0 OR NOT EXISTS "${structural_object}")
  message(FATAL_ERROR "Structural native object emission failed:\n${structural_codegen_output}\n${structural_codegen_error}")
endif()
if(WIN32)
  execute_process(COMMAND "${CXX_COMPILER}" "${structural_object}" "${RAZ_RUNTIME_LIB}" ${raz_runtime_link_deps} -o "${structural_executable}" RESULT_VARIABLE structural_link_result OUTPUT_VARIABLE structural_link_output ERROR_VARIABLE structural_link_error)
else()
  execute_process(COMMAND "${CXX_COMPILER}" "${structural_object}" "${RAZ_RUNTIME_LIB}" ${raz_runtime_link_deps} -pthread -o "${structural_executable}" RESULT_VARIABLE structural_link_result OUTPUT_VARIABLE structural_link_output ERROR_VARIABLE structural_link_error)
endif()
if(NOT structural_link_result EQUAL 0 OR NOT EXISTS "${structural_executable}")
  message(FATAL_ERROR "Structural native link failed:\n${structural_link_output}\n${structural_link_error}")
endif()
execute_process(COMMAND "${structural_executable}" RESULT_VARIABLE structural_result)
if(NOT structural_result EQUAL 42)
  message(FATAL_ERROR "Structural native executable returned ${structural_result}, expected 42")
endif()

# Fixed-array iteration parity: by-value iteration over a local array must
# preserve element type, continue/break ownership, and native execution.
set(array_for_smoke "${WORK_ROOT}/array-for-smoke")
file(MAKE_DIRECTORY "${array_for_smoke}")
file(WRITE "${array_for_smoke}/main.rz" [=[public fn main() -> i64 {
    i64 values[6] = [10, 11, 3, 8, 13, 99];
    i64 total = 0;
    for value in values {
        if (value == 3) {
            continue;
        }
        total += value;
        if (total == 42) {
            break;
        }
    }
    return total;
}
]=])
set(array_for_ir "${array_for_smoke}/array-for-output.fir")
execute_process(COMMAND "${stage2_executable}" "main.rz" "array-for-output.fir" WORKING_DIRECTORY "${array_for_smoke}" RESULT_VARIABLE array_for_compile_result)
if(NOT array_for_compile_result EQUAL 0 OR NOT EXISTS "${array_for_ir}")
  message(FATAL_ERROR "Native Stage 2 fixed-array for compilation failed with ${array_for_compile_result}")
endif()
if(WIN32)
  set(array_for_object "${array_for_smoke}/array-for-smoke.obj")
  set(array_for_executable "${array_for_smoke}/array-for-smoke.exe")
  execute_process(COMMAND "${optimized_forge_codegen}" "${array_for_ir}" "--emit-coff=${array_for_object}" --abi=windows RESULT_VARIABLE array_for_codegen_result OUTPUT_VARIABLE array_for_codegen_output ERROR_VARIABLE array_for_codegen_error)
else()
  set(array_for_object "${array_for_smoke}/array-for-smoke.o")
  set(array_for_executable "${array_for_smoke}/array-for-smoke")
  execute_process(COMMAND "${optimized_forge_codegen}" "${array_for_ir}" "--emit-elf=${array_for_object}" --abi=sysv RESULT_VARIABLE array_for_codegen_result OUTPUT_VARIABLE array_for_codegen_output ERROR_VARIABLE array_for_codegen_error)
endif()
if(NOT array_for_codegen_result EQUAL 0 OR NOT EXISTS "${array_for_object}")
  message(FATAL_ERROR "Fixed-array for native object emission failed:\n${array_for_codegen_output}\n${array_for_codegen_error}")
endif()
if(WIN32)
  execute_process(COMMAND "${CXX_COMPILER}" "${array_for_object}" "${RAZ_RUNTIME_LIB}" ${raz_runtime_link_deps} -o "${array_for_executable}" RESULT_VARIABLE array_for_link_result OUTPUT_VARIABLE array_for_link_output ERROR_VARIABLE array_for_link_error)
else()
  execute_process(COMMAND "${CXX_COMPILER}" "${array_for_object}" "${RAZ_RUNTIME_LIB}" ${raz_runtime_link_deps} -pthread -o "${array_for_executable}" RESULT_VARIABLE array_for_link_result OUTPUT_VARIABLE array_for_link_output ERROR_VARIABLE array_for_link_error)
endif()
if(NOT array_for_link_result EQUAL 0 OR NOT EXISTS "${array_for_executable}")
  message(FATAL_ERROR "Fixed-array for native link failed:\n${array_for_link_output}\n${array_for_link_error}")
endif()
execute_process(COMMAND "${array_for_executable}" RESULT_VARIABLE array_for_result)
if(NOT array_for_result EQUAL 42)
  message(FATAL_ERROR "Fixed-array for native executable returned ${array_for_result}, expected 42")
endif()
file(WRITE "${array_for_smoke}/invalid-array-for.rz" [=[public fn main() -> i64 {
    i64 value = 42;
    for item in value {
        return item;
    }
    return 0;
}
]=])
execute_process(COMMAND "${stage2_executable}" "invalid-array-for.rz" "invalid-array-for.fir" WORKING_DIRECTORY "${array_for_smoke}" RESULT_VARIABLE invalid_array_for_result)
if(invalid_array_for_result EQUAL 0 OR EXISTS "${array_for_smoke}/invalid-array-for.fir")
  message(FATAL_ERROR "Native Stage 2 accepted fixed-array iteration over a scalar")
endif()

# Match must be exhaustive unless a wildcard is present.
file(WRITE "${structural_smoke}/invalid-match.rz" [=[enum Mode { Read, Write, Execute }
public fn main() -> i64 {
    Mode mode = Mode::Read;
    match mode {
        Mode::Read => { return 1; },
        Mode::Write => { return 2; },
    }
}
]=])
execute_process(COMMAND "${stage2_executable}" "invalid-match.rz" "invalid-match.fir" WORKING_DIRECTORY "${structural_smoke}" RESULT_VARIABLE invalid_match_result)
if(invalid_match_result EQUAL 0 OR EXISTS "${structural_smoke}/invalid-match.fir")
  message(FATAL_ERROR "Native Stage 2 accepted a non-exhaustive unit-enum match")
endif()

# Payload-enum parity: tagged aggregate construction, payload bindings,
# wildcard payload positions, and exhaustive matching.
set(payload_enum_smoke "${WORK_ROOT}/payload-enum-smoke")
file(MAKE_DIRECTORY "${payload_enum_smoke}")
file(WRITE "${payload_enum_smoke}/main.rz" [=[enum Packet {
    Empty,
    Value(i64),
    Pair(i64, bool),
}

fn read(Packet packet) -> i64 {
    match packet {
        Packet::Empty => { return 0; },
        Packet::Value(value) => { return value; },
        Packet::Pair(value, enabled) => {
            if (enabled) {
                return value + 1;
            }
            return 0;
        },
    }
}

public fn main() -> i64 {
    Packet first = Packet::Value(20);
    Packet second = Packet::Pair(21, true);
    if (read(first) + read(second) == 42) {
        return 42;
    }
    return 1;
}
]=])
set(payload_enum_ir "${payload_enum_smoke}/payload-enum-output.fir")
execute_process(COMMAND "${stage2_executable}" "main.rz" "payload-enum-output.fir" WORKING_DIRECTORY "${payload_enum_smoke}" RESULT_VARIABLE payload_enum_compile_result)
if(NOT payload_enum_compile_result EQUAL 0 OR NOT EXISTS "${payload_enum_ir}")
  message(FATAL_ERROR "Native Stage 2 payload-enum compilation failed with ${payload_enum_compile_result}")
endif()
if(WIN32)
  set(payload_enum_object "${payload_enum_smoke}/payload-enum-smoke.obj")
  set(payload_enum_executable "${payload_enum_smoke}/payload-enum-smoke.exe")
  execute_process(COMMAND "${optimized_forge_codegen}" "${payload_enum_ir}" "--emit-coff=${payload_enum_object}" --abi=windows RESULT_VARIABLE payload_enum_codegen_result OUTPUT_VARIABLE payload_enum_codegen_output ERROR_VARIABLE payload_enum_codegen_error)
else()
  set(payload_enum_object "${payload_enum_smoke}/payload-enum-smoke.o")
  set(payload_enum_executable "${payload_enum_smoke}/payload-enum-smoke")
  execute_process(COMMAND "${optimized_forge_codegen}" "${payload_enum_ir}" "--emit-elf=${payload_enum_object}" --abi=sysv RESULT_VARIABLE payload_enum_codegen_result OUTPUT_VARIABLE payload_enum_codegen_output ERROR_VARIABLE payload_enum_codegen_error)
endif()
if(NOT payload_enum_codegen_result EQUAL 0 OR NOT EXISTS "${payload_enum_object}")
  message(FATAL_ERROR "Payload-enum native object emission failed:\n${payload_enum_codegen_output}\n${payload_enum_codegen_error}")
endif()
if(WIN32)
  execute_process(COMMAND "${CXX_COMPILER}" "${payload_enum_object}" "${RAZ_RUNTIME_LIB}" ${raz_runtime_link_deps} -o "${payload_enum_executable}" RESULT_VARIABLE payload_enum_link_result OUTPUT_VARIABLE payload_enum_link_output ERROR_VARIABLE payload_enum_link_error)
else()
  execute_process(COMMAND "${CXX_COMPILER}" "${payload_enum_object}" "${RAZ_RUNTIME_LIB}" ${raz_runtime_link_deps} -pthread -o "${payload_enum_executable}" RESULT_VARIABLE payload_enum_link_result OUTPUT_VARIABLE payload_enum_link_error ERROR_VARIABLE payload_enum_link_error)
endif()
if(NOT payload_enum_link_result EQUAL 0 OR NOT EXISTS "${payload_enum_executable}")
  message(FATAL_ERROR "Payload-enum native link failed:\n${payload_enum_link_output}\n${payload_enum_link_error}")
endif()
execute_process(COMMAND "${payload_enum_executable}" RESULT_VARIABLE payload_enum_result)
if(NOT payload_enum_result EQUAL 42)
  message(FATAL_ERROR "Payload-enum native executable returned ${payload_enum_result}, expected 42")
endif()

# Defer parity: lexical LIFO cleanup on normal scope exit, return, and loop exit.
set(defer_smoke "${WORK_ROOT}/defer-smoke")
file(MAKE_DIRECTORY "${defer_smoke}")
file(WRITE "${defer_smoke}/main.rz" [=[fn cleanup(i64 input) -> i64 {
    i64 value = input;
    defer value += 1;
    {
        defer value *= 2;
        value += 3;
    }
    return value;
}

fn loop_cleanup(i64 count) -> i64 {
    i64 value = 0;
    while (value < count) {
        defer value += 10;
        value += 1;
        break;
    }
    return value;
}

fn nested_cleanup(i64 input) -> i64 {
    i64 value = input;
    defer {
        defer value += 3;
        value *= 2;
    }
    return value;
}

public fn main() -> i64 {
    i64 total = cleanup(4);
    total += loop_cleanup(5);
    total += nested_cleanup(4);
    if (total == 37) {
        return 42;
    }
    return 1;
}
]=])
set(defer_ir "${defer_smoke}/defer-output.fir")
execute_process(COMMAND "${stage2_executable}" "main.rz" "defer-output.fir" WORKING_DIRECTORY "${defer_smoke}" RESULT_VARIABLE defer_compile_result)
if(NOT defer_compile_result EQUAL 0 OR NOT EXISTS "${defer_ir}")
  message(FATAL_ERROR "Native Stage 2 defer compilation failed with ${defer_compile_result}")
endif()
if(WIN32)
  set(defer_object "${defer_smoke}/defer-smoke.obj")
  set(defer_executable "${defer_smoke}/defer-smoke.exe")
  execute_process(COMMAND "${optimized_forge_codegen}" "${defer_ir}" "--emit-coff=${defer_object}" --abi=windows RESULT_VARIABLE defer_codegen_result OUTPUT_VARIABLE defer_codegen_output ERROR_VARIABLE defer_codegen_error)
else()
  set(defer_object "${defer_smoke}/defer-smoke.o")
  set(defer_executable "${defer_smoke}/defer-smoke")
  execute_process(COMMAND "${optimized_forge_codegen}" "${defer_ir}" "--emit-elf=${defer_object}" --abi=sysv RESULT_VARIABLE defer_codegen_result OUTPUT_VARIABLE defer_codegen_output ERROR_VARIABLE defer_codegen_error)
endif()
if(NOT defer_codegen_result EQUAL 0 OR NOT EXISTS "${defer_object}")
  message(FATAL_ERROR "Defer native object emission failed:\n${defer_codegen_output}\n${defer_codegen_error}")
endif()
if(WIN32)
  execute_process(COMMAND "${CXX_COMPILER}" "${defer_object}" "${RAZ_RUNTIME_LIB}" ${raz_runtime_link_deps} -o "${defer_executable}" RESULT_VARIABLE defer_link_result OUTPUT_VARIABLE defer_link_output ERROR_VARIABLE defer_link_error)
else()
  execute_process(COMMAND "${CXX_COMPILER}" "${defer_object}" "${RAZ_RUNTIME_LIB}" ${raz_runtime_link_deps} -pthread -o "${defer_executable}" RESULT_VARIABLE defer_link_result OUTPUT_VARIABLE defer_link_output ERROR_VARIABLE defer_link_error)
endif()
if(NOT defer_link_result EQUAL 0 OR NOT EXISTS "${defer_executable}")
  message(FATAL_ERROR "Defer native link failed:\n${defer_link_output}\n${defer_link_error}")
endif()
execute_process(COMMAND "${defer_executable}" RESULT_VARIABLE defer_result)
if(NOT defer_result EQUAL 42)
  message(FATAL_ERROR "Defer native executable returned ${defer_result}, expected 42")
endif()

# A deferred action may not itself transfer control.
file(WRITE "${defer_smoke}/invalid-defer-control.rz" [=[public fn main() -> i64 {
    defer return 1;
    return 42;
}
]=])
execute_process(COMMAND "${stage2_executable}" "invalid-defer-control.rz" "invalid-defer-control.fir" WORKING_DIRECTORY "${defer_smoke}" RESULT_VARIABLE invalid_defer_result)
if(invalid_defer_result EQUAL 0 OR EXISTS "${defer_smoke}/invalid-defer-control.fir")
  message(FATAL_ERROR "Native Stage 2 accepted control transfer from defer")
endif()

# Multi-file/module syntax parity. The explicit manifest controls module order;
# imports are validated syntax in the self-host compiler while package
# dependency resolution remains the project driver's responsibility.
set(module_smoke "${WORK_ROOT}/module-smoke")
file(MAKE_DIRECTORY "${module_smoke}")
file(WRITE "${module_smoke}/util.rz" [=[public const i64 BASE = 40;

fn answer() -> i64 {
    return BASE + 2;
}
]=])
file(WRITE "${module_smoke}/main.rz" [=[import util::math;

public fn main() -> i64 {
    return answer();
}
]=])
file(WRITE "${module_smoke}/package.txt" "util.rz\nmain.rz\n")
set(module_ir "${module_smoke}/module-output.fir")
execute_process(COMMAND "${stage2_executable}" "package.txt" "module-output.fir" WORKING_DIRECTORY "${module_smoke}" RESULT_VARIABLE module_compile_result)
if(NOT module_compile_result EQUAL 0 OR NOT EXISTS "${module_ir}")
  message(FATAL_ERROR "Native Stage 2 multi-file/import compilation failed with ${module_compile_result}")
endif()
if(WIN32)
  set(module_object "${module_smoke}/module-smoke.obj")
  set(module_executable "${module_smoke}/module-smoke.exe")
  execute_process(COMMAND "${optimized_forge_codegen}" "${module_ir}" "--emit-coff=${module_object}" --abi=windows RESULT_VARIABLE module_codegen_result OUTPUT_VARIABLE module_codegen_output ERROR_VARIABLE module_codegen_error)
else()
  set(module_object "${module_smoke}/module-smoke.o")
  set(module_executable "${module_smoke}/module-smoke")
  execute_process(COMMAND "${optimized_forge_codegen}" "${module_ir}" "--emit-elf=${module_object}" --abi=sysv RESULT_VARIABLE module_codegen_result OUTPUT_VARIABLE module_codegen_output ERROR_VARIABLE module_codegen_error)
endif()
if(NOT module_codegen_result EQUAL 0 OR NOT EXISTS "${module_object}")
  message(FATAL_ERROR "Module native object emission failed:\n${module_codegen_output}\n${module_codegen_error}")
endif()
if(WIN32)
  execute_process(COMMAND "${CXX_COMPILER}" "${module_object}" "${RAZ_RUNTIME_LIB}" ${raz_runtime_link_deps} -o "${module_executable}" RESULT_VARIABLE module_link_result OUTPUT_VARIABLE module_link_output ERROR_VARIABLE module_link_error)
else()
  execute_process(COMMAND "${CXX_COMPILER}" "${module_object}" "${RAZ_RUNTIME_LIB}" ${raz_runtime_link_deps} -pthread -o "${module_executable}" RESULT_VARIABLE module_link_result OUTPUT_VARIABLE module_link_output ERROR_VARIABLE module_link_error)
endif()
if(NOT module_link_result EQUAL 0 OR NOT EXISTS "${module_executable}")
  message(FATAL_ERROR "Module native link failed:\n${module_link_output}\n${module_link_error}")
endif()
execute_process(COMMAND "${module_executable}" RESULT_VARIABLE module_result)
if(NOT module_result EQUAL 42)
  message(FATAL_ERROR "Module native executable returned ${module_result}, expected 42")
endif()

file(WRITE "${module_smoke}/invalid-import.rz" "import util::;\npublic fn main() -> i64 { return 42; }\n")
execute_process(COMMAND "${stage2_executable}" "invalid-import.rz" "invalid-import.fir" WORKING_DIRECTORY "${module_smoke}" RESULT_VARIABLE invalid_import_result)
if(invalid_import_result EQUAL 0 OR EXISTS "${module_smoke}/invalid-import.fir")
  message(FATAL_ERROR "Native Stage 2 accepted malformed import syntax")
endif()

# Floating-point parity: preserve f64 through parameters, returns, local arena
# storage, arithmetic, compound assignment, and comparisons all the way to
# Forge native code.
set(float_smoke "${WORK_ROOT}/float-smoke")
file(MAKE_DIRECTORY "${float_smoke}")
file(WRITE "${float_smoke}/main.rz" [=[fn blend(f64 left, f64 right) -> f64 {
    return left * right + 0.625;
}

public fn main() -> i64 {
    f64 value = blend(1.5, 2.25);
    value += 0.5;
    value -= 0.5;
    value *= 2.0;
    value /= 2.0;
    f64 scientific = -1.25e1;
    i64 integer = 41;
    f64 converted = integer as f64;
    converted += 1.75;
    i64 truncated = converted as i64;
    f64 roundtrip = truncated as f64;
    if (value > 3.99 && value < 4.01 && scientific < -12.4 && scientific > -12.6 && truncated == 42 && roundtrip == 42.0) {
        return 42;
    }
    return 1;
}
]=])
set(float_ir "${float_smoke}/float-output.fir")
execute_process(
  COMMAND "${stage2_executable}" "main.rz" "float-output.fir"
  WORKING_DIRECTORY "${float_smoke}"
  RESULT_VARIABLE float_compile_result)
if(NOT float_compile_result EQUAL 0 OR NOT EXISTS "${float_ir}")
  message(FATAL_ERROR "Native Stage 2 f64 compilation failed with ${float_compile_result}")
endif()
file(READ "${float_ir}" float_ir_text)
foreach(required_float_text IN ITEMS "func @blend(%p0: f64, %p1: f64) -> f64" "const f64 1.5" "const f64 1.25e1" "mul f64" "div f64" "cmp.gt f64" "cmp.lt f64" "int_to_float.signed f64" "float_to_int.signed i64" "raz_rt_stage1_arena_get_f64" "raz_rt_stage1_arena_set_f64")
  string(FIND "${float_ir_text}" "${required_float_text}" float_text_position)
  if(float_text_position EQUAL -1)
    message(FATAL_ERROR "F64 Forge IR is missing '${required_float_text}'")
  endif()
endforeach()

if(WIN32)
  execute_process(
    COMMAND "${optimized_forge_codegen}" "${float_ir}" "--emit-coff=${float_object}" --abi=windows
    RESULT_VARIABLE float_codegen_result
    OUTPUT_VARIABLE float_codegen_output
    ERROR_VARIABLE float_codegen_error)
else()
  execute_process(
    COMMAND "${optimized_forge_codegen}" "${float_ir}" "--emit-elf=${float_object}" --abi=sysv
    RESULT_VARIABLE float_codegen_result
    OUTPUT_VARIABLE float_codegen_output
    ERROR_VARIABLE float_codegen_error)
endif()
if(NOT float_codegen_result EQUAL 0 OR NOT EXISTS "${float_object}")
  message(FATAL_ERROR "F64 native object emission failed:\n${float_codegen_output}\n${float_codegen_error}")
endif()

if(WIN32)
  execute_process(
    COMMAND "${CXX_COMPILER}" "${float_object}" "${RAZ_RUNTIME_LIB}" ${raz_runtime_link_deps} -o "${float_executable}"
    RESULT_VARIABLE float_link_result
    OUTPUT_VARIABLE float_link_output
    ERROR_VARIABLE float_link_error)
else()
  execute_process(
    COMMAND "${CXX_COMPILER}" "${float_object}" "${RAZ_RUNTIME_LIB}" ${raz_runtime_link_deps} -pthread -o "${float_executable}"
    RESULT_VARIABLE float_link_result
    OUTPUT_VARIABLE float_link_output
    ERROR_VARIABLE float_link_error)
endif()
if(NOT float_link_result EQUAL 0 OR NOT EXISTS "${float_executable}")
  message(FATAL_ERROR "F64 native link failed:\n${float_link_output}\n${float_link_error}")
endif()
execute_process(COMMAND "${float_executable}" RESULT_VARIABLE float_result)
if(NOT float_result EQUAL 42)
  message(FATAL_ERROR "Native f64 executable returned ${float_result}, expected 42")
endif()


# Integer width/sign parity: Stage 2 must preserve physical widths and signedness
# through locals, parameters/returns, arithmetic, shifts, comparisons, and casts.
set(width_smoke "${WORK_ROOT}/integer-width-smoke")
file(MAKE_DIRECTORY "${width_smoke}")
file(WRITE "${width_smoke}/main.rz" [=[fn increment8(u8 value) -> u8 {
    return value + 1;
}

fn halve16(i16 value) -> i16 {
    return value / 2;
}

fn narrow16(u16 value) -> u8 {
    return value;
}

public fn main() -> i64 {
    u8 narrow = 250;
    u8 quotient = narrow / 2;
    u8 remainder = narrow % 7;
    u8 shifted = narrow >> 1;
    u16 unsigned16 = 65530;
    unsigned16 >>= 1;
    u32 wide32 = 0xF0000000 as u32;
    wide32 >>= 28;
    i32 signed32 = (-64) as i32;
    signed32 >>= 3;
    u64 wide64 = (-1) as u64;
    wide64 >>= 63;
    usize size = (-1) as usize;
    size >>= 63;
    i8 signed8 = (-8) as i8;
    signed8 >>= 2;
    i16 signed16 = (-10) as i16;
    i16 half = halve16(signed16);
    i64 sign_extended = signed8 as i64;
    i64 zero_extended = narrow as i64;
    u8 truncated = 258 as u8;
    u8 bumped = increment8(41);
    u16 implicit_source = 300;
    u8 mixed_width = narrow + implicit_source;
    u8 implicit_init = implicit_source;
    implicit_init = implicit_source;
    u16 call_source = 297;
    u8 implicit_call = increment8(call_source);
    u8 implicit_return = narrow16(258);
    u8 wrapped = 255;
    wrapped += 1;
    i8 signed_wrapped = 127;
    signed_wrapped += 1;
    f64 floating = (bumped as u32) as f64;
    u32 roundtrip = floating as u32;
    if (narrow > 100 && quotient == 125 && remainder == 5 && shifted == 125 && unsigned16 == 32765 && wide32 == 15 && (signed32 as i64) == -8 && wide64 == 1 && size == 1 && (signed8 as i64) == -2 && (half as i64) == -5 && sign_extended == -2 && zero_extended == 250 && truncated == 2 && bumped == 42 && mixed_width == 38 && implicit_init == 44 && implicit_call == 42 && implicit_return == 2 && wrapped == 0 && (signed_wrapped as i64) == -128 && roundtrip == 42) {
        return 42;
    }
    return 1;
}
]=])
set(width_ir "${width_smoke}/integer-width-output.fir")
execute_process(
  COMMAND "${stage2_executable}" "main.rz" "integer-width-output.fir"
  WORKING_DIRECTORY "${width_smoke}"
  RESULT_VARIABLE width_compile_result)
if(NOT width_compile_result EQUAL 0 OR NOT EXISTS "${width_ir}")
  message(FATAL_ERROR "Native Stage 2 integer-width compilation failed with ${width_compile_result}")
endif()
file(READ "${width_ir}" width_ir_text)
foreach(required_width_text IN ITEMS
    "func @increment8(%p0: i8) -> i8"
    "func @halve16(%p0: i16) -> i16"
    "func @narrow16(%p0: i16) -> i8"
    "div.unsigned i8"
    "rem.unsigned i8"
    "shr.unsigned i8"
    "shr.unsigned i16"
    "shr.unsigned i32"
    "shr.signed i32"
    "shr.unsigned i64"
    "cmp.ugt i8"
    "truncate i8"
    "zero_extend i64"
    "sign_extend i64"
    "int_to_float.unsigned f64"
    "float_to_int.unsigned i32")
  string(FIND "${width_ir_text}" "${required_width_text}" width_text_position)
  if(width_text_position EQUAL -1)
    message(FATAL_ERROR "Integer-width Forge IR is missing '${required_width_text}'")
  endif()
endforeach()

if(WIN32)
  execute_process(
    COMMAND "${optimized_forge_codegen}" "${width_ir}" "--emit-coff=${width_object}" --abi=windows
    RESULT_VARIABLE width_codegen_result
    OUTPUT_VARIABLE width_codegen_output
    ERROR_VARIABLE width_codegen_error)
else()
  execute_process(
    COMMAND "${optimized_forge_codegen}" "${width_ir}" "--emit-elf=${width_object}" --abi=sysv
    RESULT_VARIABLE width_codegen_result
    OUTPUT_VARIABLE width_codegen_output
    ERROR_VARIABLE width_codegen_error)
endif()
if(NOT width_codegen_result EQUAL 0 OR NOT EXISTS "${width_object}")
  message(FATAL_ERROR "Integer-width native object emission failed:\n${width_codegen_output}\n${width_codegen_error}")
endif()

if(WIN32)
  execute_process(
    COMMAND "${CXX_COMPILER}" "${width_object}" "${RAZ_RUNTIME_LIB}" ${raz_runtime_link_deps} -o "${width_executable}"
    RESULT_VARIABLE width_link_result
    OUTPUT_VARIABLE width_link_output
    ERROR_VARIABLE width_link_error)
else()
  execute_process(
    COMMAND "${CXX_COMPILER}" "${width_object}" "${RAZ_RUNTIME_LIB}" ${raz_runtime_link_deps} -pthread -o "${width_executable}"
    RESULT_VARIABLE width_link_result
    OUTPUT_VARIABLE width_link_output
    ERROR_VARIABLE width_link_error)
endif()
if(NOT width_link_result EQUAL 0 OR NOT EXISTS "${width_executable}")
  message(FATAL_ERROR "Integer-width native link failed:\n${width_link_output}\n${width_link_error}")
endif()
execute_process(COMMAND "${width_executable}" RESULT_VARIABLE width_result)
if(NOT width_result EQUAL 42)
  message(FATAL_ERROR "Native integer-width executable returned ${width_result}, expected 42")
endif()

# Match the production cast boundary: full-range unsigned 64-bit values cannot
# currently use ordinary scalar int/float conversion instructions.
file(WRITE "${width_smoke}/invalid-u64-float.rz" [=[public fn main() -> i64 {
    u64 value = 1;
    f64 converted = value as f64;
    return 0;
}
]=])
execute_process(
  COMMAND "${stage2_executable}" "invalid-u64-float.rz" "invalid-u64-float.fir"
  WORKING_DIRECTORY "${width_smoke}"
  RESULT_VARIABLE invalid_u64_float_result)
if(invalid_u64_float_result EQUAL 0 OR EXISTS "${width_smoke}/invalid-u64-float.fir")
  message(FATAL_ERROR "Native Stage 2 accepted unsupported u64-to-f64 conversion")
endif()

message(STATUS "Complete: Stage 1 emitted ${stage2_ir_size}-byte Stage 2; Stage 2 compiled scalar/control-flow, f64/cast, and full integer-width parity programs; all native executables returned 42")

# Stable slice parity: shared/mutable slice construction, length, indexing,
# call/return transport, aggregate fields, and mutation must work in the Raz-written compiler itself.
set(slice_smoke "${WORK_DIR}/slice-smoke")
file(MAKE_DIRECTORY "${slice_smoke}")
file(WRITE "${slice_smoke}/main.rz" [=[struct Window {
    i64[] values;
}

fn sum(i64[] values) -> i64 {
    i64 total = 0;
    i64 index = 0;
    while index < values.length {
        total += values[index];
        index += 1;
    }
    return total;
}

fn increment_first(i64[]mut values) {
    values[0] += 10;
}

fn identity(i64[] values) -> i64[] {
    return values;
}

fn inspect(Window window) -> i64 {
    return window.values.length;
}

public fn main() -> i64 {
    i64 values[4] = [1, 2, 3, 4];
    i64[] view = &values;
    i64[] roundtrip = identity(view);
    i64 total = sum(roundtrip);
    Window window = Window(roundtrip);
    i64[]mut edit = &mut values;
    increment_first(edit);
    return total + values[0] + inspect(window) + roundtrip[1];
}
]=])
set(slice_ir "${slice_smoke}/slice-output.fir")
set(slice_object "${slice_smoke}/slice-smoke${CMAKE_CXX_OUTPUT_EXTENSION}")
if(WIN32)
  set(slice_object "${slice_smoke}/slice-smoke.obj")
  set(slice_executable "${slice_smoke}/slice-smoke.exe")
else()
  set(slice_object "${slice_smoke}/slice-smoke.o")
  set(slice_executable "${slice_smoke}/slice-smoke")
endif()
execute_process(
  COMMAND "${stage2_executable}" "main.rz" "slice-output.fir"
  WORKING_DIRECTORY "${slice_smoke}"
  RESULT_VARIABLE slice_compile_result)
if(NOT slice_compile_result EQUAL 0 OR NOT EXISTS "${slice_ir}")
  message(FATAL_ERROR "Native Stage 2 slice compilation failed with ${slice_compile_result}")
endif()
if(WIN32)
  execute_process(
    COMMAND "${optimized_forge_codegen}" "${slice_ir}" "--emit-coff=${slice_object}" --abi=windows
    RESULT_VARIABLE slice_codegen_result OUTPUT_VARIABLE slice_codegen_output ERROR_VARIABLE slice_codegen_error)
else()
  execute_process(
    COMMAND "${optimized_forge_codegen}" "${slice_ir}" "--emit-elf=${slice_object}" --abi=sysv
    RESULT_VARIABLE slice_codegen_result OUTPUT_VARIABLE slice_codegen_output ERROR_VARIABLE slice_codegen_error)
endif()
if(NOT slice_codegen_result EQUAL 0 OR NOT EXISTS "${slice_object}")
  message(FATAL_ERROR "Slice native object emission failed:\n${slice_codegen_output}\n${slice_codegen_error}")
endif()
if(WIN32)
  execute_process(
    COMMAND "${CXX_COMPILER}" "${slice_object}" "${RAZ_RUNTIME_LIB}" ${raz_runtime_link_deps} ws2_32.lib bcrypt.lib -o "${slice_executable}"
    RESULT_VARIABLE slice_link_result OUTPUT_VARIABLE slice_link_output ERROR_VARIABLE slice_link_error)
else()
  execute_process(
    COMMAND "${CXX_COMPILER}" "${slice_object}" "${RAZ_RUNTIME_LIB}" ${raz_runtime_link_deps} -pthread -o "${slice_executable}"
    RESULT_VARIABLE slice_link_result OUTPUT_VARIABLE slice_link_output ERROR_VARIABLE slice_link_error)
endif()
if(NOT slice_link_result EQUAL 0 OR NOT EXISTS "${slice_executable}")
  message(FATAL_ERROR "Slice native link failed:\n${slice_link_output}\n${slice_link_error}")
endif()
execute_process(COMMAND "${slice_executable}" RESULT_VARIABLE slice_result)
if(NOT slice_result EQUAL 27)
  message(FATAL_ERROR "Native slice executable returned ${slice_result}, expected 27")
endif()

file(WRITE "${slice_smoke}/invalid-shared-mutation.rz" [=[fn invalid(i64[] values) {
    values[0] = 7;
}
public fn main() -> i64 { return 0; }
]=])
execute_process(
  COMMAND "${stage2_executable}" "invalid-shared-mutation.rz" "invalid-shared-mutation.fir"
  WORKING_DIRECTORY "${slice_smoke}"
  RESULT_VARIABLE invalid_shared_slice_result)
if(invalid_shared_slice_result EQUAL 0 OR EXISTS "${slice_smoke}/invalid-shared-mutation.fir")
  message(FATAL_ERROR "Native Stage 2 accepted mutation through a shared slice")
endif()

message(STATUS "Slice parity: shared/mutable slices, aggregate fields, return transport, length, indexing, calls, mutation, and shared-mutation rejection passed")

# B60 ownership-core parity: explicit moves, use-after-move rejection,
# reinitialization, lexical borrow release, and conflicting borrow rejection
# must execute in the Raz-written Stage 2 compiler itself.
set(ownership_smoke "${WORK_DIR}/ownership-smoke")
file(MAKE_DIRECTORY "${ownership_smoke}")
file(WRITE "${ownership_smoke}/main.rz" [=[struct Pair {
    i64 left;
    i64 right;
}

fn consume(Pair value) -> i64 {
    return value.left + value.right;
}

public fn main() -> i64 {
    Pair first = Pair(10, 20);
    Pair second = move first;
    first = Pair(5, 7);
    {
        i64& shared = &first.left;
        i64 observed = *shared;
    }
    first = Pair(6, 8);
    return consume(move second) + first.left + first.right;
}
]=])
set(ownership_ir "${ownership_smoke}/ownership-output.fir")
if(WIN32)
  set(ownership_object "${ownership_smoke}/ownership-smoke.obj")
  set(ownership_executable "${ownership_smoke}/ownership-smoke.exe")
else()
  set(ownership_object "${ownership_smoke}/ownership-smoke.o")
  set(ownership_executable "${ownership_smoke}/ownership-smoke")
endif()
execute_process(
  COMMAND "${stage2_executable}" "main.rz" "ownership-output.fir"
  WORKING_DIRECTORY "${ownership_smoke}"
  RESULT_VARIABLE ownership_compile_result)
if(NOT ownership_compile_result EQUAL 0 OR NOT EXISTS "${ownership_ir}")
  message(FATAL_ERROR "Native Stage 2 ownership compilation failed with ${ownership_compile_result}")
endif()
if(WIN32)
  execute_process(
    COMMAND "${optimized_forge_codegen}" "${ownership_ir}" "--emit-coff=${ownership_object}" --abi=windows
    RESULT_VARIABLE ownership_codegen_result OUTPUT_VARIABLE ownership_codegen_output ERROR_VARIABLE ownership_codegen_error)
else()
  execute_process(
    COMMAND "${optimized_forge_codegen}" "${ownership_ir}" "--emit-elf=${ownership_object}" --abi=sysv
    RESULT_VARIABLE ownership_codegen_result OUTPUT_VARIABLE ownership_codegen_output ERROR_VARIABLE ownership_codegen_error)
endif()
if(NOT ownership_codegen_result EQUAL 0 OR NOT EXISTS "${ownership_object}")
  message(FATAL_ERROR "Ownership native object emission failed:\n${ownership_codegen_output}\n${ownership_codegen_error}")
endif()
if(WIN32)
  execute_process(
    COMMAND "${CXX_COMPILER}" "${ownership_object}" "${RAZ_RUNTIME_LIB}" ${raz_runtime_link_deps} ws2_32.lib bcrypt.lib -o "${ownership_executable}"
    RESULT_VARIABLE ownership_link_result OUTPUT_VARIABLE ownership_link_output ERROR_VARIABLE ownership_link_error)
else()
  execute_process(
    COMMAND "${CXX_COMPILER}" "${ownership_object}" "${RAZ_RUNTIME_LIB}" ${raz_runtime_link_deps} -pthread -o "${ownership_executable}"
    RESULT_VARIABLE ownership_link_result OUTPUT_VARIABLE ownership_link_output ERROR_VARIABLE ownership_link_error)
endif()
if(NOT ownership_link_result EQUAL 0 OR NOT EXISTS "${ownership_executable}")
  message(FATAL_ERROR "Ownership native link failed:\n${ownership_link_output}\n${ownership_link_error}")
endif()
execute_process(COMMAND "${ownership_executable}" RESULT_VARIABLE ownership_result)
if(NOT ownership_result EQUAL 44)
  message(FATAL_ERROR "Native ownership executable returned ${ownership_result}, expected 44")
endif()

file(WRITE "${ownership_smoke}/invalid-use-after-move.rz" [=[struct Pair { i64 left; i64 right; }
public fn main() -> i64 {
    Pair first = Pair(1, 2);
    Pair second = move first;
    return first.left;
}
]=])
execute_process(
  COMMAND "${stage2_executable}" "invalid-use-after-move.rz" "invalid-use-after-move.fir"
  WORKING_DIRECTORY "${ownership_smoke}"
  RESULT_VARIABLE invalid_use_after_move_result)
if(invalid_use_after_move_result EQUAL 0 OR EXISTS "${ownership_smoke}/invalid-use-after-move.fir")
  message(FATAL_ERROR "Native Stage 2 accepted use after move")
endif()

file(WRITE "${ownership_smoke}/invalid-move-while-borrowed.rz" [=[struct Pair { i64 left; i64 right; }
public fn main() -> i64 {
    Pair first = Pair(1, 2);
    i64& shared = &first.left;
    Pair second = move first;
    return second.left;
}
]=])
execute_process(
  COMMAND "${stage2_executable}" "invalid-move-while-borrowed.rz" "invalid-move-while-borrowed.fir"
  WORKING_DIRECTORY "${ownership_smoke}"
  RESULT_VARIABLE invalid_move_borrowed_result)
if(invalid_move_borrowed_result EQUAL 0 OR EXISTS "${ownership_smoke}/invalid-move-while-borrowed.fir")
  message(FATAL_ERROR "Native Stage 2 accepted move while borrowed")
endif()

file(WRITE "${ownership_smoke}/invalid-double-mut-borrow.rz" [=[struct Marker { i64 value; }
public fn main() -> i64 {
    i64 value = 1;
    i64&mut first = &mut value;
    i64&mut second = &mut value;
    return 0;
}
]=])
execute_process(
  COMMAND "${stage2_executable}" "invalid-double-mut-borrow.rz" "invalid-double-mut-borrow.fir"
  WORKING_DIRECTORY "${ownership_smoke}"
  RESULT_VARIABLE invalid_double_mut_result)
if(invalid_double_mut_result EQUAL 0 OR EXISTS "${ownership_smoke}/invalid-double-mut-borrow.fir")
  message(FATAL_ERROR "Native Stage 2 accepted overlapping mutable borrows")
endif()

message(STATUS "Ownership parity: explicit move, reinitialization, lexical borrow release, use-after-move rejection, move/borrow conflict, and mutable-borrow exclusivity passed")

# B60 projection-aware ownership: disjoint fields may be borrowed/moved
# independently, while use of the moved projection remains rejected.
file(WRITE "${ownership_smoke}/partial-move.rz" [=[struct Pair { i64 left; i64 right; }
public fn main() -> i64 {
    Pair pair = Pair(10, 32);
    i64 left = move pair.left;
    i64 right = pair.right;
    pair.left = 8;
    return left + right + pair.left;
}
]=])
execute_process(
  COMMAND "${stage2_executable}" "partial-move.rz" "partial-move.fir"
  WORKING_DIRECTORY "${ownership_smoke}"
  RESULT_VARIABLE partial_move_compile_result)
if(NOT partial_move_compile_result EQUAL 0 OR NOT EXISTS "${ownership_smoke}/partial-move.fir")
  message(FATAL_ERROR "Native Stage 2 partial-move compilation failed")
endif()
if(WIN32)
  set(partial_move_object "${ownership_smoke}/partial-move.obj")
  set(partial_move_executable "${ownership_smoke}/partial-move.exe")
  execute_process(COMMAND "${optimized_forge_codegen}" "${ownership_smoke}/partial-move.fir" "--emit-coff=${partial_move_object}" --abi=windows RESULT_VARIABLE partial_move_codegen_result)
  execute_process(COMMAND "${CXX_COMPILER}" "${partial_move_object}" "${RAZ_RUNTIME_LIB}" ${raz_runtime_link_deps} ws2_32.lib bcrypt.lib -o "${partial_move_executable}" RESULT_VARIABLE partial_move_link_result)
else()
  set(partial_move_object "${ownership_smoke}/partial-move.o")
  set(partial_move_executable "${ownership_smoke}/partial-move")
  execute_process(COMMAND "${optimized_forge_codegen}" "${ownership_smoke}/partial-move.fir" "--emit-elf=${partial_move_object}" --abi=sysv RESULT_VARIABLE partial_move_codegen_result)
  execute_process(COMMAND "${CXX_COMPILER}" "${partial_move_object}" "${RAZ_RUNTIME_LIB}" ${raz_runtime_link_deps} -pthread -o "${partial_move_executable}" RESULT_VARIABLE partial_move_link_result)
endif()
if(NOT partial_move_codegen_result EQUAL 0 OR NOT partial_move_link_result EQUAL 0)
  message(FATAL_ERROR "Partial-move native build failed")
endif()
execute_process(COMMAND "${partial_move_executable}" RESULT_VARIABLE partial_move_result)
if(NOT partial_move_result EQUAL 50)
  message(FATAL_ERROR "Partial-move executable returned ${partial_move_result}, expected 50")
endif()

file(WRITE "${ownership_smoke}/disjoint-borrow.rz" [=[struct Pair { i64 left; i64 right; }
public fn main() -> i64 {
    Pair pair = Pair(20, 22);
    i64&mut left = &mut pair.left;
    i64&mut right = &mut pair.right;
    *left += 1;
    *right += 1;
    return pair.left + pair.right;
}
]=])
execute_process(
  COMMAND "${stage2_executable}" "disjoint-borrow.rz" "disjoint-borrow.fir"
  WORKING_DIRECTORY "${ownership_smoke}"
  RESULT_VARIABLE disjoint_borrow_compile_result)
if(NOT disjoint_borrow_compile_result EQUAL 0 OR NOT EXISTS "${ownership_smoke}/disjoint-borrow.fir")
  message(FATAL_ERROR "Native Stage 2 disjoint-borrow compilation failed")
endif()
if(WIN32)
  set(disjoint_borrow_object "${ownership_smoke}/disjoint-borrow.obj")
  set(disjoint_borrow_executable "${ownership_smoke}/disjoint-borrow.exe")
  execute_process(COMMAND "${optimized_forge_codegen}" "${ownership_smoke}/disjoint-borrow.fir" "--emit-coff=${disjoint_borrow_object}" --abi=windows RESULT_VARIABLE disjoint_borrow_codegen_result)
  execute_process(COMMAND "${CXX_COMPILER}" "${disjoint_borrow_object}" "${RAZ_RUNTIME_LIB}" ${raz_runtime_link_deps} ws2_32.lib bcrypt.lib -o "${disjoint_borrow_executable}" RESULT_VARIABLE disjoint_borrow_link_result)
else()
  set(disjoint_borrow_object "${ownership_smoke}/disjoint-borrow.o")
  set(disjoint_borrow_executable "${ownership_smoke}/disjoint-borrow")
  execute_process(COMMAND "${optimized_forge_codegen}" "${ownership_smoke}/disjoint-borrow.fir" "--emit-elf=${disjoint_borrow_object}" --abi=sysv RESULT_VARIABLE disjoint_borrow_codegen_result)
  execute_process(COMMAND "${CXX_COMPILER}" "${disjoint_borrow_object}" "${RAZ_RUNTIME_LIB}" ${raz_runtime_link_deps} -pthread -o "${disjoint_borrow_executable}" RESULT_VARIABLE disjoint_borrow_link_result)
endif()
if(NOT disjoint_borrow_codegen_result EQUAL 0 OR NOT disjoint_borrow_link_result EQUAL 0)
  message(FATAL_ERROR "Disjoint-borrow native build failed")
endif()
execute_process(COMMAND "${disjoint_borrow_executable}" RESULT_VARIABLE disjoint_borrow_result)
if(NOT disjoint_borrow_result EQUAL 42)
  message(FATAL_ERROR "Disjoint-borrow executable returned ${disjoint_borrow_result}, expected 42")
endif()

file(WRITE "${ownership_smoke}/invalid-partial-use.rz" [=[struct Pair { i64 left; i64 right; }
public fn main() -> i64 {
    Pair pair = Pair(10, 32);
    i64 left = move pair.left;
    return pair.left;
}
]=])
execute_process(
  COMMAND "${stage2_executable}" "invalid-partial-use.rz" "invalid-partial-use.fir"
  WORKING_DIRECTORY "${ownership_smoke}"
  RESULT_VARIABLE invalid_partial_use_result)
if(invalid_partial_use_result EQUAL 0 OR EXISTS "${ownership_smoke}/invalid-partial-use.fir")
  message(FATAL_ERROR "Native Stage 2 accepted use of a moved projection")
endif()

message(STATUS "Projection ownership: partial move/reinitialization, disjoint mutable field borrows, and moved-projection rejection passed")


# B61 executable Drop parity: initialized overwrite must destroy the replaced
# value before assignment, including partial-move and projected-field cases.
set(drop_overwrite_smoke "${WORK_ROOT}/drop-overwrite-smoke")
file(MAKE_DIRECTORY "${drop_overwrite_smoke}")

file(WRITE "${drop_overwrite_smoke}/whole.rz" [=[
extern fn raz_rt_stage1_arena_create(i64 count) -> i64;
extern fn raz_rt_stage1_arena_destroy(i64 handle);
extern fn raz_rt_stage1_arena_get(i64 handle, i64 index) -> i64;
extern fn raz_rt_stage1_arena_set(i64 handle, i64 index, i64 value);
struct Resource { i64 tracker; }
impl Drop for Resource {
    fn drop(Resource&mut self) {
        i64 count = raz_rt_stage1_arena_get(self.tracker, 0);
        raz_rt_stage1_arena_set(self.tracker, 0, count + 1);
    }
}
fn main() -> i64 {
    i64 tracker = raz_rt_stage1_arena_create(1);
    {
        Resource value = Resource(tracker);
        value = Resource(tracker);
    }
    i64 result = raz_rt_stage1_arena_get(tracker, 0);
    raz_rt_stage1_arena_destroy(tracker);
    return result;
}
]=])

file(WRITE "${drop_overwrite_smoke}/partial.rz" [=[
extern fn raz_rt_stage1_arena_create(i64 count) -> i64;
extern fn raz_rt_stage1_arena_destroy(i64 handle);
extern fn raz_rt_stage1_arena_get(i64 handle, i64 index) -> i64;
extern fn raz_rt_stage1_arena_set(i64 handle, i64 index, i64 value);
struct Resource { i64 tracker; i64 index; }
impl Drop for Resource {
    fn drop(Resource&mut self) {
        i64 count = raz_rt_stage1_arena_get(self.tracker, self.index);
        raz_rt_stage1_arena_set(self.tracker, self.index, count + 1);
    }
}
struct Pair { Resource left; Resource right; }
fn main() -> i64 {
    i64 tracker = raz_rt_stage1_arena_create(2);
    {
        Pair pair = Pair(Resource(tracker, 0), Resource(tracker, 1));
        Resource moved = move pair.left;
        pair = Pair(Resource(tracker, 0), Resource(tracker, 1));
    }
    i64 left = raz_rt_stage1_arena_get(tracker, 0);
    i64 right = raz_rt_stage1_arena_get(tracker, 1);
    raz_rt_stage1_arena_destroy(tracker);
    return left + right;
}
]=])

file(WRITE "${drop_overwrite_smoke}/field.rz" [=[
extern fn raz_rt_stage1_arena_create(i64 count) -> i64;
extern fn raz_rt_stage1_arena_destroy(i64 handle);
extern fn raz_rt_stage1_arena_get(i64 handle, i64 index) -> i64;
extern fn raz_rt_stage1_arena_set(i64 handle, i64 index, i64 value);
struct Resource { i64 tracker; i64 index; }
impl Drop for Resource {
    fn drop(Resource&mut self) {
        i64 count = raz_rt_stage1_arena_get(self.tracker, self.index);
        raz_rt_stage1_arena_set(self.tracker, self.index, count + 1);
    }
}
struct Pair { Resource left; Resource right; }
fn main() -> i64 {
    i64 tracker = raz_rt_stage1_arena_create(2);
    {
        Pair pair = Pair(Resource(tracker, 0), Resource(tracker, 1));
        pair.left = Resource(tracker, 0);
    }
    i64 left = raz_rt_stage1_arena_get(tracker, 0);
    i64 right = raz_rt_stage1_arena_get(tracker, 1);
    raz_rt_stage1_arena_destroy(tracker);
    return left + right;
}
]=])

foreach(drop_case IN ITEMS whole partial field)
  set(drop_case_ir "${drop_overwrite_smoke}/${drop_case}.fir")
  execute_process(
    COMMAND "${stage2_executable}" "${drop_case}.rz" "${drop_case}.fir"
    WORKING_DIRECTORY "${drop_overwrite_smoke}"
    RESULT_VARIABLE drop_case_compile_result)
  if(NOT drop_case_compile_result EQUAL 0 OR NOT EXISTS "${drop_case_ir}")
    message(FATAL_ERROR "Stage 2 Drop overwrite case ${drop_case} failed to compile: ${drop_case_compile_result}")
  endif()
  if(WIN32)
    set(drop_case_object "${drop_overwrite_smoke}/${drop_case}.obj")
    set(drop_case_executable "${drop_overwrite_smoke}/${drop_case}.exe")
    execute_process(COMMAND "${optimized_forge_codegen}" "${drop_case_ir}" "--emit-coff=${drop_case_object}" --abi=windows RESULT_VARIABLE drop_case_codegen_result)
    if(drop_case_codegen_result EQUAL 0)
      execute_process(COMMAND "${CXX_COMPILER}" "${drop_case_object}" "${RAZ_RUNTIME_LIB}" ${raz_runtime_link_deps} ws2_32.lib bcrypt.lib -o "${drop_case_executable}" RESULT_VARIABLE drop_case_link_result)
    endif()
  else()
    set(drop_case_object "${drop_overwrite_smoke}/${drop_case}.o")
    set(drop_case_executable "${drop_overwrite_smoke}/${drop_case}")
    execute_process(COMMAND "${optimized_forge_codegen}" "${drop_case_ir}" "--emit-elf=${drop_case_object}" --abi=sysv RESULT_VARIABLE drop_case_codegen_result)
    if(drop_case_codegen_result EQUAL 0)
      execute_process(COMMAND "${CXX_COMPILER}" "${drop_case_object}" "${RAZ_RUNTIME_LIB}" ${raz_runtime_link_deps} -pthread -o "${drop_case_executable}" RESULT_VARIABLE drop_case_link_result)
    endif()
  endif()
  if(NOT drop_case_codegen_result EQUAL 0 OR NOT drop_case_link_result EQUAL 0 OR NOT EXISTS "${drop_case_executable}")
    message(FATAL_ERROR "Native Drop overwrite case ${drop_case} failed to emit/link")
  endif()
  execute_process(COMMAND "${drop_case_executable}" RESULT_VARIABLE drop_case_result)
  if(drop_case STREQUAL "whole")
    set(drop_case_expected 2)
  elseif(drop_case STREQUAL "partial")
    set(drop_case_expected 4)
  else()
    set(drop_case_expected 3)
  endif()
  if(NOT drop_case_result EQUAL drop_case_expected)
    message(FATAL_ERROR "Native Drop overwrite case ${drop_case} returned ${drop_case_result}, expected ${drop_case_expected}")
  endif()
endforeach()
message(STATUS "Executable Drop overwrite parity: whole, partial-move, and field replacement cleanup passed")

# C1 production-parity gate: generic bounds, associated-item completeness, and
# statically dispatched generic trait implementations must work in native
# Stage 2, not only in the C++ host frontend.
set(c1_trait_smoke "${WORK_ROOT}/c1-trait-smoke")
file(MAKE_DIRECTORY "${c1_trait_smoke}")
file(WRITE "${c1_trait_smoke}/main.rz" [=[
trait Measurable { fn measure(Self& self) -> i64; }
struct Metric { i64 value; }
impl Measurable for Metric { fn measure(Metric& self) -> i64 { return self.value; } }
impl Clone for Metric { fn clone(Metric& self) -> Metric { return Metric(self.value); } }
fn preserve<T: Measurable + Clone>(T value) -> T { return value; }
fn read<T: Measurable>(T& value) -> i64 { return value.measure(); }

trait IteratorInfo {
    type Item;
    const SIZE: usize;
    fn current(Self& self) -> i64;
}
struct Counter { i64 value; }
impl IteratorInfo for Counter {
    type Item = i64;
    const SIZE: usize = 8;
    fn current(Counter& self) -> i64 { return self.value; }
}

trait Provider {
    type Item;
    fn get(Self& self) -> i64;
}
struct Box<T> { T value; }
impl<T: Clone> Provider for Box<T> {
    type Item = T;
    fn get(Box<T>& self) -> i64 { return self.value.value; }
}

fn main() -> i64 {
    Metric metric = Metric(42);
    Metric copy = preserve<Metric>(metric);
    Counter counter = Counter(42);
    Box<Metric> box = Box<Metric>(Metric(42));
    if (copy.value == 42 && read<Metric>(&copy) == 42 && counter.current() == 42 && box.get() == 42) {
        return 42;
    }
    return 1;
}
]=])
execute_process(
  COMMAND "${stage2_executable}" "main.rz" "main.fir"
  WORKING_DIRECTORY "${c1_trait_smoke}"
  RESULT_VARIABLE c1_trait_compile_result)
if(NOT c1_trait_compile_result EQUAL 0 OR NOT EXISTS "${c1_trait_smoke}/main.fir")
  message(FATAL_ERROR "C1 native Stage 2 generic-bound/trait smoke failed to compile: ${c1_trait_compile_result}")
endif()
if(WIN32)
  set(c1_trait_object "${c1_trait_smoke}/main.obj")
  set(c1_trait_executable "${c1_trait_smoke}/main.exe")
  execute_process(COMMAND "${optimized_forge_codegen}" "${c1_trait_smoke}/main.fir" "--emit-coff=${c1_trait_object}" --abi=windows RESULT_VARIABLE c1_trait_codegen_result)
  if(c1_trait_codegen_result EQUAL 0)
    execute_process(COMMAND "${CXX_COMPILER}" "${c1_trait_object}" "${RAZ_RUNTIME_LIB}" ${raz_runtime_link_deps} ws2_32.lib bcrypt.lib -o "${c1_trait_executable}" RESULT_VARIABLE c1_trait_link_result)
  endif()
else()
  set(c1_trait_object "${c1_trait_smoke}/main.o")
  set(c1_trait_executable "${c1_trait_smoke}/main")
  execute_process(COMMAND "${optimized_forge_codegen}" "${c1_trait_smoke}/main.fir" "--emit-elf=${c1_trait_object}" --abi=sysv RESULT_VARIABLE c1_trait_codegen_result)
  if(c1_trait_codegen_result EQUAL 0)
    execute_process(COMMAND "${CXX_COMPILER}" "${c1_trait_object}" "${RAZ_RUNTIME_LIB}" ${raz_runtime_link_deps} -pthread -o "${c1_trait_executable}" RESULT_VARIABLE c1_trait_link_result)
  endif()
endif()
if(NOT c1_trait_codegen_result EQUAL 0 OR NOT c1_trait_link_result EQUAL 0 OR NOT EXISTS "${c1_trait_executable}")
  message(FATAL_ERROR "C1 native Stage 2 generic-bound/trait smoke failed to emit/link")
endif()
execute_process(COMMAND "${c1_trait_executable}" RESULT_VARIABLE c1_trait_run_result)
if(NOT c1_trait_run_result EQUAL 42)
  message(FATAL_ERROR "C1 native generic-bound/trait executable returned ${c1_trait_run_result}, expected 42")
endif()

file(WRITE "${c1_trait_smoke}/invalid-bound.rz" [=[
trait Measurable { fn measure(Self& self) -> i64; }
struct Plain { i64 value; }
impl Measurable for Plain { fn measure(Plain& self) -> i64 { return self.value; } }
fn preserve<T: Measurable + Clone>(T value) -> T { return value; }
fn main() -> i64 { Plain value = Plain(42); Plain copy = preserve<Plain>(value); return copy.value; }
]=])
file(WRITE "${c1_trait_smoke}/invalid-associated.rz" [=[
trait IteratorInfo { type Item; const SIZE: usize; fn current(Self& self) -> i64; }
struct Counter { i64 value; }
impl IteratorInfo for Counter { const SIZE: usize = 8; fn current(Counter& self) -> i64 { return self.value; } }
fn main() -> i64 { return 0; }
]=])
file(WRITE "${c1_trait_smoke}/invalid-generic-associated.rz" [=[
trait Provider { type Item; fn get(Self& self) -> i64; }
struct Box<T> { T value; }
impl<T> Provider for Box<T> { fn get(Box<T>& self) -> i64 { return 42; } }
fn main() -> i64 { Box<i64> value = Box<i64>(42); return value.get(); }
]=])
file(WRITE "${c1_trait_smoke}/invalid-generic-bound.rz" [=[
trait Provider { fn get(Self& self) -> i64; }
struct Box<T> { T value; }
struct Plain { i64 value; }
impl<T: Clone> Provider for Box<T> { fn get(Box<T>& self) -> i64 { return 42; } }
fn main() -> i64 { Box<Plain> value = Box<Plain>(Plain(42)); return value.get(); }
]=])
file(WRITE "${c1_trait_smoke}/invalid-overlap.rz" [=[
trait Provider { fn get(Self& self) -> i64; }
struct Box<T> { T value; }
impl<T> Provider for Box<T> { fn get(Box<T>& self) -> i64 { return 1; } }
impl<T: Clone> Provider for Box<T> { fn other(Box<T>& self) -> i64 { return 2; } }
fn main() -> i64 { return 0; }
]=])
foreach(c1_invalid IN ITEMS invalid-bound invalid-associated invalid-generic-associated invalid-generic-bound invalid-overlap)
  file(REMOVE "${c1_trait_smoke}/${c1_invalid}.fir")
  execute_process(
    COMMAND "${stage2_executable}" "${c1_invalid}.rz" "${c1_invalid}.fir"
    WORKING_DIRECTORY "${c1_trait_smoke}"
    RESULT_VARIABLE c1_invalid_result)
  if(c1_invalid_result EQUAL 0 OR EXISTS "${c1_trait_smoke}/${c1_invalid}.fir")
    message(FATAL_ERROR "C1 native Stage 2 accepted invalid trait case ${c1_invalid}")
  endif()
endforeach()
message(STATUS "C1 self-host trait parity: generic bounds, bound method lookup, associated requirements, generic trait applicability, and overlap rejection passed")
