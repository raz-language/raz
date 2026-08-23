# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20)
project(oblink VERSION 0.4.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(OBLINK_BUILD_TESTS "Build ObLink tests" ON)

add_library(oblink_core STATIC
  src/coff.cpp
  src/link.cpp
  src/pe.cpp
)
target_include_directories(oblink_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(oblink_core PUBLIC cxx_std_20)
if(MSVC)
  target_compile_options(oblink_core PRIVATE /W4 /permissive-)
else()
  target_compile_options(oblink_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(oblink tools/oblink/main.cpp)
target_link_libraries(oblink PRIVATE oblink_core)
# Single-source the version so the CLI banner cannot drift from project().
target_compile_definitions(oblink PRIVATE OBLINK_VERSION="${PROJECT_VERSION}")

if(OBLINK_BUILD_TESTS)
  enable_testing()
  add_executable(oblink-coff-tests tests/coff_tests.cpp)
  target_link_libraries(oblink-coff-tests PRIVATE oblink_core)
  add_test(NAME oblink-coff-tests COMMAND oblink-coff-tests)

  add_executable(oblink-link-tests tests/link_tests.cpp)
  target_link_libraries(oblink-link-tests PRIVATE oblink_core)
  add_test(NAME oblink-link-tests COMMAND oblink-link-tests)

  # Keep CLI response-file support qualified: Raz uses it automatically for
  # compiler-sized Windows links to stay below cmd.exe's command-line limit.
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/oblink-help.rsp" "--help\n")
  add_test(NAME oblink-response-file COMMAND oblink "@${CMAKE_CURRENT_BINARY_DIR}/oblink-help.rsp")
endif()
