# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED RAZC OR NOT DEFINED OUTPUT OR NOT DEFINED SOURCES)
  message(FATAL_ERROR "RAZC, OUTPUT, and SOURCES are required")
endif()

file(WRITE "${OUTPUT}" "")
foreach(source IN LISTS SOURCES)
  if(NOT EXISTS "${source}")
    message(FATAL_ERROR "missing Raz source: ${source}")
  endif()
  file(READ "${source}" contents)
  file(APPEND "${OUTPUT}" "${contents}\n")
endforeach()

execute_process(
  COMMAND "${RAZC}" --check "${OUTPUT}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "stdlib dependency-closure check failed (${result}):\n${output}\n${error}")
endif()
