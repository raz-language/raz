# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED FORGE OR NOT DEFINED INPUT OR NOT DEFINED OBJECT OR NOT DEFINED ARCHIVE OR
   NOT DEFINED SOURCE OR NOT DEFINED EXECUTABLE OR NOT DEFINED CXX_COMPILER)
  message(FATAL_ERROR "missing static library test argument")
endif()
execute_process(COMMAND "${FORGE}" compile "${INPUT}" --format=elf -o "${OBJECT}"
  RESULT_VARIABLE compile_status OUTPUT_VARIABLE compile_output ERROR_VARIABLE compile_error)
if(NOT compile_status EQUAL 0)
  message(FATAL_ERROR "Forge object compile failed: ${compile_status}\n${compile_output}\n${compile_error}")
endif()
execute_process(COMMAND "${FORGE}" archive create -o "${ARCHIVE}" "${OBJECT}"
  RESULT_VARIABLE archive_status OUTPUT_VARIABLE archive_output ERROR_VARIABLE archive_error)
if(NOT archive_status EQUAL 0)
  message(FATAL_ERROR "Forge archive creation failed: ${archive_status}\n${archive_output}\n${archive_error}")
endif()
file(WRITE "${SOURCE}" "extern \"C\" long long answer();\nint main() { return answer() == 42 ? 0 : 1; }\n")
execute_process(COMMAND "${CXX_COMPILER}" "${SOURCE}" "${ARCHIVE}" -o "${EXECUTABLE}"
  RESULT_VARIABLE link_status OUTPUT_VARIABLE link_output ERROR_VARIABLE link_error)
if(NOT link_status EQUAL 0)
  message(FATAL_ERROR "static archive native link failed: ${link_status}\n${link_output}\n${link_error}")
endif()
execute_process(COMMAND "${EXECUTABLE}" RESULT_VARIABLE run_status)
if(NOT run_status EQUAL 0)
  message(FATAL_ERROR "static archive executable failed: ${run_status}")
endif()
