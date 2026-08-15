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
string(REGEX MATCH "instructions=([0-9]+)" _after_match "${output}")
set(instructions_after "${CMAKE_MATCH_1}")
string(REGEX MATCH "instructions-before-machine-opt=([0-9]+)" _before_match "${output}")
set(instructions_before "${CMAKE_MATCH_1}")
string(REGEX MATCH "machine-address-modes-folded=([0-9]+)" _folded_match "${output}")
set(address_modes_folded "${CMAKE_MATCH_1}")
string(REGEX MATCH "bytes=([0-9]+)" _bytes_match "${output}")
set(encoded_bytes "${CMAKE_MATCH_1}")
if(instructions_after STREQUAL "" OR instructions_before STREQUAL "" OR address_modes_folded STREQUAL "" OR encoded_bytes STREQUAL "")
 message(FATAL_ERROR "unable to parse address-mode statistics: ${output}")
endif()
if(address_modes_folded LESS MIN_ADDRESS_MODES_FOLDED)
 message(FATAL_ERROR "address-mode folding regression: ${address_modes_folded} < ${MIN_ADDRESS_MODES_FOLDED}")
endif()
if(NOT instructions_before EQUAL 4 OR NOT instructions_after EQUAL 3)
 message(FATAL_ERROR "address-mode instruction regression: ${instructions_after}/${instructions_before} != 3/4")
endif()
if(encoded_bytes GREATER MAX_ENCODED_BYTES)
 message(FATAL_ERROR "address-mode byte regression: ${encoded_bytes} > ${MAX_ENCODED_BYTES}")
endif()
message(STATUS "Forge address mode: instructions=${instructions_after}/${instructions_before}, folded=${address_modes_folded}, bytes=${encoded_bytes}/${MAX_ENCODED_BYTES}")
