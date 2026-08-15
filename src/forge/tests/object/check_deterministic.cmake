# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

execute_process(COMMAND "${FORGE}" compile "${INPUT}" --format=${FORMAT} -o "${OUTPUT1}" RESULT_VARIABLE r1)
if(NOT r1 EQUAL 0)
  message(FATAL_ERROR "first object emission failed")
endif()
execute_process(COMMAND "${FORGE}" compile "${INPUT}" --format=${FORMAT} -o "${OUTPUT2}" RESULT_VARIABLE r2)
if(NOT r2 EQUAL 0)
  message(FATAL_ERROR "second object emission failed")
endif()
file(SHA256 "${OUTPUT1}" h1)
file(SHA256 "${OUTPUT2}" h2)
if(NOT h1 STREQUAL h2)
  message(FATAL_ERROR "object output is not deterministic: ${h1} != ${h2}")
endif()
