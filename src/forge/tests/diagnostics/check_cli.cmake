# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

execute_process(COMMAND "${FORGE_EXE}" verify "${INPUT}" RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(result EQUAL 0)
  message(FATAL_ERROR "malformed input unexpectedly succeeded")
endif()
string(CONCAT combined "${out}" "${err}")
if(NOT combined MATCHES "invalid-source.fir:4:26: error: expected name after sigil")
  message(FATAL_ERROR "missing source-aware diagnostic:\n${combined}")
endif()
if(NOT combined MATCHES "4 \\|       %value = const i64 @")
  message(FATAL_ERROR "missing source line:\n${combined}")
endif()
