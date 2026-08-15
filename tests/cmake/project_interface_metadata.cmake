# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED RAZ_EXE OR NOT DEFINED FIXTURE_ROOT OR NOT DEFINED WORK_ROOT)
  message(FATAL_ERROR "RAZ_EXE, FIXTURE_ROOT, and WORK_ROOT are required")
endif()
file(REMOVE_RECURSE "${WORK_ROOT}")
file(MAKE_DIRECTORY "${WORK_ROOT}")
file(COPY "${FIXTURE_ROOT}/interface-dependency" DESTINATION "${WORK_ROOT}")
file(COPY "${FIXTURE_ROOT}/interface-consumer" DESTINATION "${WORK_ROOT}")
set(consumer "${WORK_ROOT}/interface-consumer")
execute_process(COMMAND "${RAZ_EXE}" build "${consumer}" --target test-host --force
  RESULT_VARIABLE first_result OUTPUT_VARIABLE first_out ERROR_VARIABLE first_err)
if(NOT first_result EQUAL 0)
  message(FATAL_ERROR "initial build failed:\n${first_out}\n${first_err}")
endif()
set(dmi "${WORK_ROOT}/interface-dependency/target/test-host/debug/modules/lib.dmi")
if(NOT EXISTS "${dmi}")
  message(FATAL_ERROR "dependency interface was not emitted: ${dmi}")
endif()
file(READ "${dmi}" interface)
foreach(expected "raz-interface-v5" "package_interface_hash=" "module_interface_hash=" "trait=public trait PackageMarker {}" "trait=public trait PackageValue { fn value(Self& self) -> i64; }" "type=public struct Token { i64 value; }" "impl=impl PackageMarker for Token {}" "impl=impl PackageValue for Token { fn value(Token& self) -> i64 { return self.value; } }" "semantic=")
  string(FIND "${interface}" "${expected}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "missing interface metadata '${expected}':\n${interface}")
  endif()
endforeach()
foreach(hidden "PackageInternal" "ModuleSecret")
  string(FIND "${interface}" "${hidden}" hidden_found)
  if(NOT hidden_found EQUAL -1)
    message(FATAL_ERROR "non-public declaration leaked into dependency interface '${hidden}':\n${interface}")
  endif()
endforeach()
execute_process(COMMAND "${RAZ_EXE}" build "${consumer}" --target test-host --verbose
  RESULT_VARIABLE fresh_result OUTPUT_VARIABLE fresh_out ERROR_VARIABLE fresh_err)
if(NOT fresh_result EQUAL 0 OR NOT fresh_out MATCHES "0 compiled, 2 fresh")
  message(FATAL_ERROR "expected fully fresh consumer build:\n${fresh_out}\n${fresh_err}")
endif()

# Dependency declarations are loaded semantically from .dmi files. A duplicate
# implementation in the consumer must therefore fail coherence before linking.
file(APPEND "${consumer}/src/main.rz" "\nimpl PackageMarker for Token {}\n")
execute_process(COMMAND "${RAZ_EXE}" check "${consumer}" --target test-host --force
  RESULT_VARIABLE overlap_result OUTPUT_VARIABLE overlap_out ERROR_VARIABLE overlap_err)
if(overlap_result EQUAL 0)
  message(FATAL_ERROR "cross-package duplicate implementation was accepted:\n${overlap_out}\n${overlap_err}")
endif()

# Restore the consumer before testing dependency invalidation.
file(READ "${FIXTURE_ROOT}/interface-consumer/src/main.rz" consumer_source)
file(WRITE "${consumer}/src/main.rz" "${consumer_source}")
# Package-private declarations are implementation details and must not invalidate
# downstream packages because they are intentionally absent from the public DMI.
file(APPEND "${WORK_ROOT}/interface-dependency/src/lib.rz" "\ntrait InternalContract {}\n")
execute_process(COMMAND "${RAZ_EXE}" build "${consumer}" --target test-host --verbose
  RESULT_VARIABLE internal_result OUTPUT_VARIABLE internal_out ERROR_VARIABLE internal_err)
if(NOT internal_result EQUAL 0)
  message(FATAL_ERROR "package-private change build failed:\n${internal_out}\n${internal_err}")
endif()
if(NOT internal_out MATCHES "Fresh[ ]+interface_consumer::main")
  message(FATAL_ERROR "package-private dependency change unnecessarily invalidated consumer:\n${internal_out}")
endif()

file(APPEND "${WORK_ROOT}/interface-dependency/src/lib.rz" "\npublic trait AddedContract {}\n")
execute_process(COMMAND "${RAZ_EXE}" build "${consumer}" --target test-host --verbose
  RESULT_VARIABLE changed_result OUTPUT_VARIABLE changed_out ERROR_VARIABLE changed_err)
if(NOT changed_result EQUAL 0)
  message(FATAL_ERROR "changed-interface build failed:\n${changed_out}\n${changed_err}")
endif()
if(NOT changed_out MATCHES "Compiling interface_consumer::main")
  message(FATAL_ERROR "consumer was not invalidated by public dependency interface change:\n${changed_out}")
endif()
