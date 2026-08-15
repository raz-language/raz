# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

execute_process(
 COMMAND "${FORGE_CODEGEN}" "${INPUT}" --stats
 RESULT_VARIABLE result
 OUTPUT_VARIABLE output
 ERROR_VARIABLE error)
if(NOT result EQUAL 0)
 message(FATAL_ERROR "forge-codegen failed: ${error}")
endif()
foreach(metric
  machine-immediate-comparisons-selected
  machine-direct-constant-returns
  machine-zeroing-idioms-selected
  machine-constant-definitions-eliminated
  machine-instructions-eliminated
  bytes)
 string(REPLACE "-" "_" variable "${metric}")
 string(REGEX MATCH "${metric}=([0-9]+)" _match "${output}")
 set(${variable} "${CMAKE_MATCH_1}")
 if(${variable} STREQUAL "")
  message(FATAL_ERROR "unable to parse ${metric}: ${output}")
 endif()
endforeach()
if(machine_immediate_comparisons_selected LESS MIN_IMMEDIATE_COMPARISONS)
 message(FATAL_ERROR "immediate comparison regression")
endif()
if(machine_direct_constant_returns LESS MIN_DIRECT_RETURNS)
 message(FATAL_ERROR "direct constant return regression")
endif()
if(machine_zeroing_idioms_selected LESS MIN_ZEROING_IDIOMS)
 message(FATAL_ERROR "zeroing idiom regression")
endif()
if(machine_constant_definitions_eliminated LESS MIN_CONSTANTS_ELIMINATED)
 message(FATAL_ERROR "constant elimination regression")
endif()
if(machine_instructions_eliminated LESS MIN_INSTRUCTIONS_ELIMINATED)
 message(FATAL_ERROR "instruction elimination regression")
endif()
if(bytes GREATER MAX_ENCODED_BYTES)
 message(FATAL_ERROR "immediate control byte regression: ${bytes} > ${MAX_ENCODED_BYTES}")
endif()
message(STATUS "Forge immediate control: comparisons=${machine_immediate_comparisons_selected}, returns=${machine_direct_constant_returns}, zeroing=${machine_zeroing_idioms_selected}, constants=${machine_constant_definitions_eliminated}, bytes=${bytes}/${MAX_ENCODED_BYTES}")
