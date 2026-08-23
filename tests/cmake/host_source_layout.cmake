# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED PROJECT_ROOT OR NOT EXISTS "${PROJECT_ROOT}/compiler/src/main.rz")
  message(FATAL_ERROR "Canonical compiler entrypoint is missing: ${PROJECT_ROOT}/compiler/src/main.rz")
endif()

# Production compiler construction is semantic and order-independent.  The
# canonical compiler must never opt into the generic legacy source-order.txt
# package mechanism.
if(EXISTS "${PROJECT_ROOT}/compiler/source-order.txt")
  message(FATAL_ERROR "Canonical compiler must not contain compiler/source-order.txt")
endif()
file(GLOB_RECURSE compiler_sources RELATIVE "${PROJECT_ROOT}/compiler" "${PROJECT_ROOT}/compiler/src/*.rz")
list(SORT compiler_sources)
list(LENGTH compiler_sources compiler_source_count)
if(compiler_source_count LESS 2)
  message(FATAL_ERROR "Expected modular compiler source set, found ${compiler_source_count} module(s)")
endif()
list(FIND compiler_sources "src/main.rz" main_index)
if(main_index EQUAL -1)
  message(FATAL_ERROR "compiler/src/main.rz is missing from semantic module discovery")
endif()
foreach(forbidden IN ITEMS
    "${PROJECT_ROOT}/frontend.rz"
    "${PROJECT_ROOT}/compiler/frontend.rz"
    "${PROJECT_ROOT}/src/bootstrap/seed/frontend.rz"
    "${PROJECT_ROOT}/src/bootstrap/seed/compiler/frontend.rz"
    "${PROJECT_ROOT}/src/bootstrap/seed/src/main.rz")
  if(EXISTS "${forbidden}")
    message(FATAL_ERROR "Redundant compiler source must not be committed: ${forbidden}")
  endif()
endforeach()
message(STATUS "Canonical semantic compiler source layout: PASS (${compiler_source_count} modules)")

if(NOT DEFINED BOOTSTRAP_SCRIPT OR NOT EXISTS "${BOOTSTRAP_SCRIPT}")
  message(FATAL_ERROR "Bootstrap driver is missing: ${BOOTSTRAP_SCRIPT}")
endif()
file(READ "${BOOTSTRAP_SCRIPT}" bootstrap_driver)
string(FIND "${bootstrap_driver}" "def compiler_modules()" semantic_discovery)
if(semantic_discovery EQUAL -1)
  message(FATAL_ERROR "Bootstrap must discover the canonical compiler source set without ordering metadata.")
endif()
string(FIND "${bootstrap_driver}" "host-source-order.txt" legacy_bootstrap_order)
if(NOT legacy_bootstrap_order EQUAL -1)
  message(FATAL_ERROR "Bootstrap must not depend on compiler/host-source-order.txt.")
endif()
string(FIND "${bootstrap_driver}" "Never recreate the old source-order.txt" semantic_contract)
if(semantic_contract EQUAL -1)
  message(FATAL_ERROR "Bootstrap semantic-module contract is missing.")
endif()
message(STATUS "Order-independent recursive bootstrap: PASS")

# Generic user packages may still opt into source-order.txt for compatibility;
# that legacy package feature is separate from the canonical compiler itself.
set(native_project_loader "${PROJECT_ROOT}/src/bootstrap/compiler/project/project.cpp")
set(raz_project_loader "${PROJECT_ROOT}/compiler/src/driver/project.rz")
foreach(loader IN ITEMS "${native_project_loader}" "${raz_project_loader}")
  if(NOT EXISTS "${loader}")
    message(FATAL_ERROR "Project loader missing: ${loader}")
  endif()
  file(READ "${loader}" loader_text)
  string(FIND "${loader_text}" "source-order.txt" source_order_support)
  if(source_order_support EQUAL -1)
    message(FATAL_ERROR "Project loader must honor optional legacy source-order.txt: ${loader}")
  endif()
endforeach()
message(STATUS "Optional package source-order compatibility: PASS")
