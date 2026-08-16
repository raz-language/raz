# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED RAZ_EXE OR NOT DEFINED SOURCE_ROOT OR NOT DEFINED WORK_ROOT)
  message(FATAL_ERROR "Query profile requires RAZ_EXE, SOURCE_ROOT, WORK_ROOT")
endif()
file(REMOVE_RECURSE "${WORK_ROOT}")
file(MAKE_DIRECTORY "${WORK_ROOT}")
file(COPY "${SOURCE_ROOT}/compiler/" DESTINATION "${WORK_ROOT}/compiler")
execute_process(
  COMMAND "${RAZ_EXE}" build "${WORK_ROOT}/compiler" --force
  RESULT_VARIABLE build_result
  OUTPUT_VARIABLE build_output
  ERROR_VARIABLE build_error)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "Compiler compiler build failed:\n${build_error}\n${build_output}")
endif()
if(WIN32)
  set(seed "${WORK_ROOT}/compiler/target/host/debug/raz-compiler.exe")
else()
  set(seed "${WORK_ROOT}/compiler/target/host/debug/raz-compiler")
endif()
if(NOT EXISTS "${seed}")
  message(FATAL_ERROR "Compiler compiler missing: ${seed}")
endif()
execute_process(
  COMMAND "${seed}" --profile-queries --check "${SOURCE_ROOT}/tests/layout/query_cache_stress.rz"
  WORKING_DIRECTORY "${WORK_ROOT}"
  RESULT_VARIABLE profile_result
  OUTPUT_VARIABLE profile_output
  ERROR_VARIABLE profile_error)
if(NOT profile_result EQUAL 0)
  message(FATAL_ERROR "Profile regression failed:\n${profile_error}\n${profile_output}")
endif()
set(profile_path "${WORK_ROOT}/compiler-query-profile.txt")
if(NOT EXISTS "${profile_path}")
  message(FATAL_ERROR "Compiler query profile was not emitted")
endif()
file(READ "${profile_path}" profile_text)
if(NOT profile_text MATCHES "layout ([1-9][0-9]*) ([0-9]+)")
  message(FATAL_ERROR "Layout cache did not record any hits:\n${profile_text}")
endif()
if(NOT profile_text MATCHES "total ([1-9][0-9]*) ([0-9]+)")
  message(FATAL_ERROR "Unified query profile did not report total hits:\n${profile_text}")
endif()


# Pass AA: repeated trait-method calls first probe inherent-method lookup. The
# epoch-aware negative cache should turn those repeated verified misses into
# resolution-cache hits without hiding methods materialized later in analysis.
execute_process(
  COMMAND "${seed}" --profile-queries --check "${SOURCE_ROOT}/tests/resolution/negative_method_cache_stress.rz"
  WORKING_DIRECTORY "${WORK_ROOT}"
  RESULT_VARIABLE negative_profile_result
  OUTPUT_VARIABLE negative_profile_output
  ERROR_VARIABLE negative_profile_error)
if(NOT negative_profile_result EQUAL 0)
  message(FATAL_ERROR "Negative-resolution regression failed:
${negative_profile_error}
${negative_profile_output}")
endif()
file(READ "${profile_path}" negative_profile_text)
if(NOT negative_profile_text MATCHES "resolution ([1-9][0-9]*) ([0-9]+)")
  message(FATAL_ERROR "Negative method cache did not record resolution hits:
${negative_profile_text}")
endif()

# Pass AC: ordinary top-level function resolution is epoch-aware and cached.
# Repeated calls to the same visible symbol must generate cache hits while
# preserving host compiler / compiler acceptance parity.
execute_process(
  COMMAND "${seed}" --profile-queries --check "${SOURCE_ROOT}/tests/resolution/top_level_symbol_cache_stress.rz"
  WORKING_DIRECTORY "${WORK_ROOT}"
  RESULT_VARIABLE symbol_profile_result
  OUTPUT_VARIABLE symbol_profile_output
  ERROR_VARIABLE symbol_profile_error)
if(NOT symbol_profile_result EQUAL 0)
  message(FATAL_ERROR "Symbol-cache regression failed:\n${symbol_profile_error}\n${symbol_profile_output}")
endif()
file(READ "${profile_path}" symbol_profile_text)
if(NOT symbol_profile_text MATCHES "symbol ([1-9][0-9]*) ([0-9]+)")
  message(FATAL_ERROR "Top-level symbol cache did not record hits:\n${symbol_profile_text}")
endif()
