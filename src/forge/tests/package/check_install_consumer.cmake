# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED BUILD_DIR OR NOT DEFINED SOURCE_DIR OR NOT DEFINED CMAKE_COMMAND_PATH)
  message(FATAL_ERROR "BUILD_DIR, SOURCE_DIR, and CMAKE_COMMAND_PATH are required")
endif()

set(prefix "${BUILD_DIR}/install-consumer-prefix")
set(consumer_build "${BUILD_DIR}/install-consumer-build")
set(c_consumer_build "${BUILD_DIR}/install-c-consumer-build")

# Isolated package consumers must use the same generator and toolchain as the
# parent Forge build. Letting a CI runner auto-select a generator can choose an
# unavailable tool (for example NMake on a Ninja-based Windows runner).
set(common_configure_args "-DCMAKE_PREFIX_PATH=${prefix}")
if(DEFINED PARENT_GENERATOR AND NOT PARENT_GENERATOR STREQUAL "")
  list(APPEND common_configure_args -G "${PARENT_GENERATOR}")
endif()
if(DEFINED PARENT_GENERATOR_PLATFORM AND NOT PARENT_GENERATOR_PLATFORM STREQUAL "")
  list(APPEND common_configure_args -A "${PARENT_GENERATOR_PLATFORM}")
endif()
if(DEFINED PARENT_GENERATOR_TOOLSET AND NOT PARENT_GENERATOR_TOOLSET STREQUAL "")
  list(APPEND common_configure_args -T "${PARENT_GENERATOR_TOOLSET}")
endif()
if(DEFINED PARENT_C_COMPILER AND NOT PARENT_C_COMPILER STREQUAL "")
  list(APPEND common_configure_args "-DCMAKE_C_COMPILER=${PARENT_C_COMPILER}")
endif()
if(DEFINED PARENT_CXX_COMPILER AND NOT PARENT_CXX_COMPILER STREQUAL "")
  list(APPEND common_configure_args "-DCMAKE_CXX_COMPILER=${PARENT_CXX_COMPILER}")
endif()
if(DEFINED PARENT_BUILD_TYPE AND NOT PARENT_BUILD_TYPE STREQUAL "")
  list(APPEND common_configure_args "-DCMAKE_BUILD_TYPE=${PARENT_BUILD_TYPE}")
endif()
if(BUILD_DIR MATCHES "asan-ubsan")
  list(APPEND common_configure_args
    "-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer"
    "-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer"
    "-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined")
endif()

set(common_build_args "")
if(DEFINED PARENT_CONFIG AND NOT PARENT_CONFIG STREQUAL "")
  list(APPEND common_build_args --config "${PARENT_CONFIG}")
endif()

file(REMOVE_RECURSE "${prefix}" "${consumer_build}" "${c_consumer_build}")

set(install_args --install "${BUILD_DIR}" --prefix "${prefix}")
if(DEFINED PARENT_CONFIG AND NOT PARENT_CONFIG STREQUAL "")
  list(APPEND install_args --config "${PARENT_CONFIG}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND_PATH}" ${install_args}
  RESULT_VARIABLE install_result)
if(NOT install_result EQUAL 0)
  message(FATAL_ERROR "Forge install step failed: ${install_result}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND_PATH}"
          -S "${SOURCE_DIR}/tests/package/consumer"
          -B "${consumer_build}"
          ${common_configure_args}
  RESULT_VARIABLE configure_result)
if(NOT configure_result EQUAL 0)
  message(FATAL_ERROR "Forge consumer configure failed: ${configure_result}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND_PATH}" --build "${consumer_build}" ${common_build_args}
  RESULT_VARIABLE build_result)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "Forge consumer build failed: ${build_result}")
endif()

if(WIN32)
  set(executable "${consumer_build}/forge-consumer.exe")
  if(DEFINED PARENT_CONFIG AND EXISTS "${consumer_build}/${PARENT_CONFIG}/forge-consumer.exe")
    set(executable "${consumer_build}/${PARENT_CONFIG}/forge-consumer.exe")
  endif()
else()
  set(executable "${consumer_build}/forge-consumer")
endif()
execute_process(COMMAND "${executable}" RESULT_VARIABLE run_result OUTPUT_VARIABLE output)
if(NOT run_result EQUAL 0)
  message(FATAL_ERROR "Forge consumer failed: ${run_result}")
endif()
string(STRIP "${output}" output)
if(NOT output STREQUAL "2.0.0")
  message(FATAL_ERROR "Unexpected installed Forge version: ${output}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND_PATH}"
          -S "${SOURCE_DIR}/tests/package/c-consumer"
          -B "${c_consumer_build}"
          ${common_configure_args}
  RESULT_VARIABLE c_configure_result)
if(NOT c_configure_result EQUAL 0)
  message(FATAL_ERROR "Forge C consumer configure failed: ${c_configure_result}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND_PATH}" --build "${c_consumer_build}" ${common_build_args}
  RESULT_VARIABLE c_build_result)
if(NOT c_build_result EQUAL 0)
  message(FATAL_ERROR "Forge C consumer build failed: ${c_build_result}")
endif()

if(WIN32)
  set(c_executable "${c_consumer_build}/forge-c-consumer.exe")
  if(DEFINED PARENT_CONFIG AND EXISTS "${c_consumer_build}/${PARENT_CONFIG}/forge-c-consumer.exe")
    set(c_executable "${c_consumer_build}/${PARENT_CONFIG}/forge-c-consumer.exe")
  endif()
else()
  set(c_executable "${c_consumer_build}/forge-c-consumer")
endif()
execute_process(COMMAND "${c_executable}" RESULT_VARIABLE c_run_result OUTPUT_VARIABLE c_output)
if(NOT c_run_result EQUAL 0 OR NOT c_output MATCHES "module @installed_c_api")
  message(FATAL_ERROR "Forge installed C API consumer failed: ${c_run_result} ${c_output}")
endif()
