# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

execute_process(
  COMMAND "${CODEGEN}" "${INPUT}" --machine-ir --stats
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "machine liveness codegen failed: ${error}")
endif()
foreach(metric machine-dead-instructions-eliminated machine-dead-comparisons-eliminated machine-liveness-iterations machine-cross-block-live-values bytes)
  string(REGEX MATCH "${metric}=([0-9]+)" match "${output}")
  if(NOT match)
    message(FATAL_ERROR "missing ${metric} in output: ${output}")
  endif()
  set(${metric} "${CMAKE_MATCH_1}")
endforeach()
if(machine-dead-instructions-eliminated LESS 2)
  message(FATAL_ERROR "expected at least two globally dead machine instructions after immediate folding: ${output}")
endif()
if(machine-dead-comparisons-eliminated LESS 1)
  message(FATAL_ERROR "expected one dead comparison to be removed: ${output}")
endif()
if(machine-liveness-iterations LESS 1)
  message(FATAL_ERROR "expected liveness analysis to run: ${output}")
endif()
if(machine-cross-block-live-values LESS 1)
  message(FATAL_ERROR "expected a live value transfer across a CFG edge: ${output}")
endif()
if(bytes GREATER 50)
  message(FATAL_ERROR "machine liveness encoded-byte regression: ${bytes} > 50")
endif()
if(output MATCHES "dead_add|dead_compare")
  message(FATAL_ERROR "dead definitions survived optimized machine IR: ${output}")
endif()
