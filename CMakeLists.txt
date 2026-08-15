# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.24)
project(Raz VERSION 1.0.0 LANGUAGES CXX)

option(RAZ_BUILD_TESTS "Build Raz tests" ON)
option(RAZ_BUILD_TOOLS "Build Raz tools" ON)
option(RAZ_ENABLE_WERROR "Treat Raz warnings as errors" OFF)
option(RAZ_USE_BUNDLED_FORGE "Use the bundled C++ Forge production backend" ON)
option(RAZ_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(RAZ_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Windows SDK/MSVC headers intentionally deprecate portable ISO C/C++ CRT APIs
# such as fopen() and getenv() in favor of Microsoft-specific *_s variants.
# Raz keeps the portable APIs at platform boundaries, so suppress only that
# vendor deprecation diagnostic instead of weakening /WX globally. This also
# applies when CMake drives clang-cl, where MSVC is true.
if(MSVC)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

if(WIN32)
  # Keep legacy Windows SDK macros from colliding with standard C++ and Raz
  # implementation identifiers in every native target, not only the runtime.
  add_compile_definitions(NOMINMAX=1 WIN32_LEAN_AND_MEAN=1)
endif()

if(RAZ_ENABLE_ASAN OR RAZ_ENABLE_UBSAN)
  if(MSVC)
    if(RAZ_ENABLE_ASAN)
      add_compile_options(/fsanitize=address)
      add_link_options(/fsanitize=address)
    endif()
    if(RAZ_ENABLE_UBSAN)
      message(FATAL_ERROR "RAZ_ENABLE_UBSAN is not supported with MSVC")
    endif()
  else()
    set(_raz_sanitizers "")
    if(RAZ_ENABLE_ASAN)
      list(APPEND _raz_sanitizers address)
    endif()
    if(RAZ_ENABLE_UBSAN)
      list(APPEND _raz_sanitizers undefined)
    endif()
    list(JOIN _raz_sanitizers "," _raz_sanitizer_list)
    add_compile_options(-fno-omit-frame-pointer -fsanitize=${_raz_sanitizer_list})
    add_link_options(-fsanitize=${_raz_sanitizer_list})
  endif()
endif()

if(RAZ_USE_BUNDLED_FORGE)
  set(FORGE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(FORGE_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
  set(FORGE_BUILD_FUZZERS OFF CACHE BOOL "" FORCE)
  set(FORGE_INSTALL_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(FORGE_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
  add_subdirectory(src/forge EXCLUDE_FROM_ALL)
else()
  find_package(Forge 2.0 CONFIG REQUIRED)
endif()

add_subdirectory(src/runtime)
add_subdirectory(src/bootstrap)

if(RAZ_BUILD_TESTS)
  include(CTest)
  enable_testing()
  add_subdirectory(tests)
endif()
