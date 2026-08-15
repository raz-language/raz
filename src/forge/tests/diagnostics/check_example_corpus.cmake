# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED FORGE OR NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "FORGE and SOURCE_DIR are required")
endif()
file(GLOB examples "${SOURCE_DIR}/examples/*.fir")
list(SORT examples)
list(LENGTH examples example_count)
if(example_count LESS 40)
  message(FATAL_ERROR "expected at least 40 release corpus examples, found ${example_count}")
endif()
foreach(example IN LISTS examples)
  execute_process(COMMAND "${FORGE}" verify "${example}" OUTPUT_QUIET ERROR_VARIABLE verify_error RESULT_VARIABLE verify_result)
  if(NOT verify_result EQUAL 0)
    message(FATAL_ERROR "release corpus verification failed for ${example}: ${verify_error}")
  endif()
endforeach()
message(STATUS "verified ${example_count} Forge IR release corpus files")
