# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "SOURCE_DIR is required")
endif()

file(GLOB root_ir "${SOURCE_DIR}/*.fir")
if(root_ir)
  message(FATAL_ERROR "Forge IR examples must live under examples/: ${root_ir}")
endif()

foreach(required
    ".gitignore"
    ".editorconfig"
    ".clang-format"
    "README.md"
    "CHANGELOG.md"
    "CONTRIBUTING.md"
    "SECURITY.md"
    "examples/README.md"
    "docs/release-readiness.md")
  if(NOT EXISTS "${SOURCE_DIR}/${required}")
    message(FATAL_ERROR "required repository file is missing: ${required}")
  endif()
endforeach()

file(GLOB_RECURSE tracked_build_debris
  "${SOURCE_DIR}/CMakeCache.txt"
  "${SOURCE_DIR}/*.o"
  "${SOURCE_DIR}/*.obj"
  "${SOURCE_DIR}/*.a"
  "${SOURCE_DIR}/*.lib"
  "${SOURCE_DIR}/*.exe")
foreach(path IN LISTS tracked_build_debris)
  if(NOT path MATCHES "/build/")
    message(FATAL_ERROR "generated build artifact found outside build/: ${path}")
  endif()
endforeach()

# Every maintained code and build-script file must carry the project license header.
file(GLOB_RECURSE licensed_sources
  "${SOURCE_DIR}/*.cpp"
  "${SOURCE_DIR}/*.hpp"
  "${SOURCE_DIR}/*.c"
  "${SOURCE_DIR}/*.h"
  "${SOURCE_DIR}/*.cmake"
  "${SOURCE_DIR}/*.sh"
  "${SOURCE_DIR}/*.ps1"
  "${SOURCE_DIR}/*.py"
  "${SOURCE_DIR}/*.in"
  "${SOURCE_DIR}/CMakeLists.txt")
foreach(path IN LISTS licensed_sources)
  if(path MATCHES "/build/")
    continue()
  endif()
  file(READ "${path}" source_text LIMIT 1024)
  if(NOT source_text MATCHES "Copyright 2026 Mario Vinciguerra")
    message(FATAL_ERROR "source file is missing the copyright header: ${path}")
  endif()
  if(NOT source_text MATCHES "SPDX-License-Identifier: Apache-2[.]0")
    message(FATAL_ERROR "source file is missing the Apache-2.0 SPDX header: ${path}")
  endif()
endforeach()

file(READ "${SOURCE_DIR}/docs/architecture.md" architecture)
if(architecture MATCHES "v0\\.[0-9]+")
  message(FATAL_ERROR "architecture documentation contains a stale pre-1.0 version label")
endif()

message(STATUS "repository hygiene gate passed")
