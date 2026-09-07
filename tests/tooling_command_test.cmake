# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

# Smoke-tests one production tooling command against a copy of the fixture
# package.
#
# Every command here operates on the current working directory rather than a
# path argument, so each step runs with WORKING_DIRECTORY "${WORK}". The
# commands that take a path (fmt, lint, doc) receive one explicitly.

if(NOT DEFINED RAZ OR NOT DEFINED SOURCE OR NOT DEFINED WORK OR NOT DEFINED MODE)
  message(FATAL_ERROR "missing test arguments")
endif()
file(REMOVE_RECURSE "${WORK}")
file(COPY "${SOURCE}/" DESTINATION "${WORK}")

if(MODE STREQUAL "fmt")
  execute_process(COMMAND "${RAZ}" fmt . --check WORKING_DIRECTORY "${WORK}" RESULT_VARIABLE before)
  if(before EQUAL 0)
    message(FATAL_ERROR "format check unexpectedly passed before formatting")
  endif()
  execute_process(COMMAND "${RAZ}" fmt . WORKING_DIRECTORY "${WORK}" RESULT_VARIABLE format_result)
  if(NOT format_result EQUAL 0)
    message(FATAL_ERROR "format command failed")
  endif()
  execute_process(COMMAND "${RAZ}" fmt . --check WORKING_DIRECTORY "${WORK}" RESULT_VARIABLE after)
  if(NOT after EQUAL 0)
    message(FATAL_ERROR "format check failed after formatting")
  endif()
elseif(MODE STREQUAL "lint")
  execute_process(COMMAND "${RAZ}" lint . WORKING_DIRECTORY "${WORK}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "lint command failed: ${errors}${output}")
  endif()
elseif(MODE STREQUAL "doc")
  execute_process(COMMAND "${RAZ}" doc src/main.rz api.md WORKING_DIRECTORY "${WORK}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
  if(NOT result EQUAL 0 OR NOT EXISTS "${WORK}/api.md")
    message(FATAL_ERROR "doc command failed: ${errors}${output}")
  endif()
  file(SIZE "${WORK}/api.md" doc_size)
  if(doc_size EQUAL 0)
    message(FATAL_ERROR "doc command produced an empty document")
  endif()
elseif(MODE STREQUAL "lock")
  file(REMOVE "${WORK}/raz.lock")
  execute_process(COMMAND "${RAZ}" lock WORKING_DIRECTORY "${WORK}" RESULT_VARIABLE result)
  if(NOT result EQUAL 0 OR NOT EXISTS "${WORK}/raz.lock")
    message(FATAL_ERROR "lock command failed")
  endif()
  file(READ "${WORK}/raz.lock" lockfile)
  if(NOT lockfile MATCHES "version = 1" OR NOT lockfile MATCHES "tooling-package")
    message(FATAL_ERROR "lockfile contents invalid")
  endif()
elseif(MODE STREQUAL "bench")
  execute_process(COMMAND "${RAZ}" bench --iterations=2 WORKING_DIRECTORY "${WORK}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "benchmark command failed: ${errors}${output}")
  endif()
elseif(MODE STREQUAL "profile")
  execute_process(COMMAND "${RAZ}" profile WORKING_DIRECTORY "${WORK}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
  if(NOT result EQUAL 0 OR NOT EXISTS "${WORK}/target/profile/compiler-query-profile.txt")
    message(FATAL_ERROR "profile command failed: ${errors}${output}")
  endif()
elseif(MODE STREQUAL "publish")
  set(registry "${WORK}/local-registry")
  file(MAKE_DIRECTORY "${registry}")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "RAZ_REGISTRY_PUBLISH_DIR=${registry}" "${RAZ}" publish
    WORKING_DIRECTORY "${WORK}" RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
  if(NOT result EQUAL 0 OR NOT EXISTS "${registry}/index.txt"
     OR NOT EXISTS "${registry}/packages/tooling-package/0.1.0.dpk")
    message(FATAL_ERROR "publish command failed: ${errors}${output}")
  endif()
  if(NOT output MATCHES "tooling-package@0.1.0")
    message(FATAL_ERROR "publish did not report the published package: ${output}")
  endif()
elseif(MODE STREQUAL "graph")
  execute_process(COMMAND "${RAZ}" graph WORKING_DIRECTORY "${WORK}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
  if(NOT result EQUAL 0 OR NOT output MATCHES "tooling-package")
    message(FATAL_ERROR "dependency graph command failed: ${errors}${output}")
  endif()
elseif(MODE STREQUAL "doctor")
  execute_process(COMMAND "${RAZ}" doctor WORKING_DIRECTORY "${WORK}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
  if(NOT result EQUAL 0 OR NOT output MATCHES "Raz toolchain is ready")
    message(FATAL_ERROR "doctor command failed: ${errors}${output}")
  endif()
elseif(MODE STREQUAL "cache")
  execute_process(COMMAND "${RAZ}" build WORKING_DIRECTORY "${WORK}" RESULT_VARIABLE build_result)
  execute_process(COMMAND "${RAZ}" cache status WORKING_DIRECTORY "${WORK}"
    RESULT_VARIABLE inspect_result OUTPUT_VARIABLE inspect_output)
  if(NOT build_result EQUAL 0 OR NOT inspect_result EQUAL 0 OR NOT inspect_output MATCHES "store")
    message(FATAL_ERROR "cache inspection failed: ${inspect_output}")
  endif()
  execute_process(COMMAND "${RAZ}" cache clean WORKING_DIRECTORY "${WORK}"
    RESULT_VARIABLE clean_result OUTPUT_VARIABLE clean_output)
  if(NOT clean_result EQUAL 0)
    message(FATAL_ERROR "cache clean failed: ${clean_output}")
  endif()
else()
  message(FATAL_ERROR "unknown tooling command mode: ${MODE}")
endif()
