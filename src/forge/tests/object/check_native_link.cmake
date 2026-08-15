# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

execute_process(COMMAND "${FORGE}" compile "${INPUT}" --format=elf -o "${OBJECT}" RESULT_VARIABLE compile_result)
if(NOT compile_result EQUAL 0)
  message(FATAL_ERROR "Forge object compilation failed")
endif()
execute_process(COMMAND "${C_COMPILER}" "${OBJECT}" -o "${EXECUTABLE}" RESULT_VARIABLE link_result ERROR_VARIABLE link_error)
if(NOT link_result EQUAL 0)
  message(FATAL_ERROR "native link failed: ${link_error}")
endif()
execute_process(COMMAND "${EXECUTABLE}" RESULT_VARIABLE run_result)
if(NOT run_result EQUAL 0)
  message(FATAL_ERROR "linked executable returned ${run_result}")
endif()
