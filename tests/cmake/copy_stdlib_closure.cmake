# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

function(raz_copy_stdlib_closure)
  if(NOT DEFINED PYTHON_EXECUTABLE OR NOT PYTHON_EXECUTABLE)
    message(FATAL_ERROR "stdlib closure copy requires PYTHON_EXECUTABLE")
  endif()
  execute_process(
    COMMAND "${PYTHON_EXECUTABLE}" "${SOURCE_ROOT}/tests/python/copy-stdlib-closure.py"
      --library "${SOURCE_ROOT}/library"
      --entry "${WORK_ROOT}/src/main.rz"
      --output "${WORK_ROOT}/src"
    RESULT_VARIABLE closure_result
    OUTPUT_VARIABLE closure_output
    ERROR_VARIABLE closure_error)
  if(NOT closure_result EQUAL 0)
    message(FATAL_ERROR "Stdlib dependency closure failed:\n${closure_error}\n${closure_output}")
  endif()
endfunction()
