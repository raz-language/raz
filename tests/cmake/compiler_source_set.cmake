# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

function(materialize_compiler_source SOURCE_ROOT OUTPUT_PATH)
  find_program(RAZ_TEST_PYTHON NAMES python3 python REQUIRED)
  execute_process(
    COMMAND "${RAZ_TEST_PYTHON}" "${SOURCE_ROOT}/tools/compiler_sources.py"
            --root "${SOURCE_ROOT}" --materialize "${OUTPUT_PATH}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "Could not discover/materialize compiler semantic modules:\n${output}\n${error}")
  endif()
  if(NOT EXISTS "${OUTPUT_PATH}")
    message(FATAL_ERROR "Compiler source materialization was not produced: ${OUTPUT_PATH}")
  endif()
endfunction()
