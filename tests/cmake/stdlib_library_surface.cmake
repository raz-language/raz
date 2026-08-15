# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED RAZC_EXE OR NOT DEFINED SOURCE_ROOT)
  message(FATAL_ERROR "RAZC_EXE and SOURCE_ROOT are required")
endif()
file(GLOB_RECURSE modules "${SOURCE_ROOT}/library/core/*.rz" "${SOURCE_ROOT}/library/alloc/*.rz" "${SOURCE_ROOT}/library/std/*.rz")
list(SORT modules)
if(NOT modules)
  message(FATAL_ERROR "no standard library modules found")
endif()
foreach(module IN LISTS modules)
  # Individual implementation modules can legally extend types declared in sibling
  # modules, so semantic-checking a file in isolation is not a valid package-level
  # qualification. Lex every shipped module here; package/runtime qualification
  # tests exercise semantic dependency resolution across the complete library graph.
  execute_process(COMMAND "${RAZC_EXE}" --tokens "${module}" RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "standard library module lexical qualification failed: ${module}\n${output}\n${error}")
  endif()
endforeach()
message(STATUS "qualified ${modules_LENGTH} standard library modules")
