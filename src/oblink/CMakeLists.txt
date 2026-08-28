# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20)
project(oblink VERSION 0.4.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(OBLINK_BUILD_TESTS "Build ObLink tests" ON)
option(OBLINK_ENABLE_WERROR "Treat ObLink warnings as errors" OFF)

add_library(oblink_core STATIC
  src/coff.cpp
  src/link.cpp
  src/pe.cpp
)
target_include_directories(oblink_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(oblink_core PUBLIC cxx_std_20)
if(MSVC)
  target_compile_options(oblink_core PRIVATE /W4 /permissive-)
  if(OBLINK_ENABLE_WERROR)
    target_compile_options(oblink_core PRIVATE /WX)
  endif()
else()
  target_compile_options(oblink_core PRIVATE -Wall -Wextra -Wpedantic)
  if(OBLINK_ENABLE_WERROR)
    target_compile_options(oblink_core PRIVATE -Werror)
  endif()
endif()


function(oblink_apply_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive-)
    if(OBLINK_ENABLE_WERROR)
      target_compile_options(${target} PRIVATE /WX)
    endif()
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    if(OBLINK_ENABLE_WERROR)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
endfunction()

add_executable(oblink tools/oblink/main.cpp)
target_link_libraries(oblink PRIVATE oblink_core)
oblink_apply_warnings(oblink)
# Single-source the version so the CLI banner cannot drift from project().
target_compile_definitions(oblink PRIVATE OBLINK_VERSION="${PROJECT_VERSION}")

if(OBLINK_BUILD_TESTS)
  enable_testing()
  add_executable(oblink-coff-tests tests/coff_tests.cpp)
  target_link_libraries(oblink-coff-tests PRIVATE oblink_core)
  oblink_apply_warnings(oblink-coff-tests)
  add_test(NAME oblink-coff-tests COMMAND oblink-coff-tests)

  add_executable(oblink-link-tests tests/link_tests.cpp)
  target_link_libraries(oblink-link-tests PRIVATE oblink_core)
  oblink_apply_warnings(oblink-link-tests)
  add_test(NAME oblink-link-tests COMMAND oblink-link-tests)

  add_executable(oblink-pe-tests tests/pe_tests.cpp)
  target_link_libraries(oblink-pe-tests PRIVATE oblink_core)
  oblink_apply_warnings(oblink-pe-tests)
  add_test(NAME oblink-pe-tests COMMAND oblink-pe-tests)

  # Keep CLI response-file support qualified: Raz uses it automatically for
  # compiler-sized Windows links to stay below cmd.exe's command-line limit.
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/oblink-help.rsp" "--help\n")
  add_test(NAME oblink-response-file COMMAND oblink "@${CMAKE_CURRENT_BINARY_DIR}/oblink-help.rsp")
  add_test(NAME oblink-version COMMAND oblink --version)
  set_tests_properties(oblink-version PROPERTIES PASS_REGULAR_EXPRESSION "oblink ${PROJECT_VERSION}")

  # When ObLink is built inside the Raz workspace, Forge's canonical CLI target
  # is available too. Qualify the real public boundary rather than relying on
  # private in-process structures: Forge IR -> COFF -> ObLink -> deterministic PE.
  if(TARGET forge-cli)
    add_test(NAME oblink-forge-compat COMMAND ${CMAKE_COMMAND}
      -DFORGE=$<TARGET_FILE:forge-cli>
      -DOBLINK=$<TARGET_FILE:oblink>
      -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/forge-compat
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_forge_compat.cmake)
  endif()
endif()
