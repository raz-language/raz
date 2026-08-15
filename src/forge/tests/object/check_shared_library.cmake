# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED FORGE OR NOT DEFINED INPUT OR NOT DEFINED OBJECT OR NOT DEFINED LIBRARY OR
   NOT DEFINED SOURCE OR NOT DEFINED EXECUTABLE OR NOT DEFINED CXX_COMPILER)
  message(FATAL_ERROR "missing shared library test argument")
endif()
execute_process(COMMAND "${FORGE}" compile "${INPUT}" --format=elf -o "${OBJECT}"
  RESULT_VARIABLE compile_status OUTPUT_VARIABLE compile_output ERROR_VARIABLE compile_error)
if(NOT compile_status EQUAL 0)
  message(FATAL_ERROR "Forge object compile failed: ${compile_status}\n${compile_output}\n${compile_error}")
endif()
execute_process(COMMAND "${FORGE}" link-shared --linker=${CXX_COMPILER} -o "${LIBRARY}" "${OBJECT}"
  RESULT_VARIABLE library_status OUTPUT_VARIABLE library_output ERROR_VARIABLE library_error)
if(NOT library_status EQUAL 0)
  message(FATAL_ERROR "Forge shared library link failed: ${library_status}\n${library_output}\n${library_error}")
endif()
file(WRITE "${SOURCE}" "#include <dlfcn.h>\nint main() { void* h = dlopen(\"${LIBRARY}\", RTLD_NOW); if (!h) return 2; auto f = reinterpret_cast<long long(*)()>(dlsym(h, \"answer\")); return f && f() == 42 ? 0 : 3; }\n")
execute_process(COMMAND "${CXX_COMPILER}" "${SOURCE}" -ldl -o "${EXECUTABLE}"
  RESULT_VARIABLE link_status OUTPUT_VARIABLE link_output ERROR_VARIABLE link_error)
if(NOT link_status EQUAL 0)
  message(FATAL_ERROR "shared library consumer link failed: ${link_status}\n${link_output}\n${link_error}")
endif()
execute_process(COMMAND "${EXECUTABLE}" RESULT_VARIABLE run_status)
if(NOT run_status EQUAL 0)
  message(FATAL_ERROR "shared library consumer failed: ${run_status}")
endif()
