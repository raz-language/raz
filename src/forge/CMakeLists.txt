# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.21)
project(Forge
 VERSION 2.0.0
 DESCRIPTION "Verified deterministic compiler core and x86-64 backend"
 LANGUAGES CXX)
option(FORGE_BUILD_TESTS "Build Forge tests" ON)
option(FORGE_BUILD_TOOLS "Build Forge command-line tools" ON)
option(FORGE_BUILD_FUZZERS "Build deterministic fuzz/smoke targets" OFF)
option(FORGE_ENABLE_WERROR "Treat warnings as errors" OFF)
option(FORGE_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(FORGE_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(FORGE_INSTALL_EXAMPLES "Install Forge IR examples" ON)
option(FORGE_INSTALL_DOCS "Install Forge documentation" ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/include/forge/version.hpp.in ${CMAKE_CURRENT_BINARY_DIR}/generated/forge/version.hpp @ONLY)

if(FORGE_ENABLE_ASAN OR FORGE_ENABLE_UBSAN)
 if(MSVC)
  if(FORGE_ENABLE_ASAN)
   add_compile_options(/fsanitize=address)
   add_link_options(/fsanitize=address)
  endif()
  if(FORGE_ENABLE_UBSAN)
   message(FATAL_ERROR "FORGE_ENABLE_UBSAN is not supported by MSVC")
  endif()
 else()
  set(FORGE_SANITIZERS "")
  if(FORGE_ENABLE_ASAN)
   list(APPEND FORGE_SANITIZERS address)
  endif()
  if(FORGE_ENABLE_UBSAN)
   list(APPEND FORGE_SANITIZERS undefined)
  endif()
  list(JOIN FORGE_SANITIZERS "," FORGE_SANITIZER_LIST)
  add_compile_options(-fsanitize=${FORGE_SANITIZER_LIST} -fno-omit-frame-pointer)
  add_link_options(-fsanitize=${FORGE_SANITIZER_LIST})
 endif()
endif()
add_library(forge
 src/diagnostics/format.cpp
 src/frontend/source_manager.cpp src/frontend/diagnostic_engine.cpp src/frontend/symbol_table.cpp src/frontend/semantic_context.cpp src/frontend/control_flow.cpp
 src/ir/context.cpp src/ir/module.cpp src/ir/builder.cpp src/ir/source_map.cpp src/ir/incremental.cpp src/ir/artifact_cache.cpp src/ir/build_driver.cpp
  src/ir/dependency_build.cpp src/capi/forge.cpp src/ir/lexer.cpp src/ir/parser.cpp src/ir/printer.cpp src/ir/verifier.cpp src/ir/binary.cpp
 src/analysis/function_analysis.cpp src/pass/pass.cpp src/pass/pipeline.cpp src/transforms/scalar.cpp
 src/machine/module.cpp src/machine/lower.cpp src/machine/liveness.cpp src/machine/optimize.cpp src/machine/register_allocation.cpp src/machine/verifier.cpp src/codegen/x86_64/encoder.cpp
 src/jit/engine.cpp src/jit/invoke.cpp src/object/elf.cpp src/object/coff.cpp src/object/archive.cpp src/object/incremental.cpp src/object/native_link.cpp src/interpreter/interpreter.cpp src/runtime/bindings.cpp src/target/data_layout.cpp src/target/abi.cpp)
add_library(Forge::forge ALIAS forge)
set_target_properties(forge PROPERTIES
 VERSION ${PROJECT_VERSION}
 SOVERSION ${PROJECT_VERSION_MAJOR}
 POSITION_INDEPENDENT_CODE ON
 EXPORT_NAME forge)
target_compile_features(forge PUBLIC cxx_std_20)
target_include_directories(forge PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated> $<INSTALL_INTERFACE:include>)
if(MSVC)
 target_compile_options(forge PRIVATE /W4 /permissive-)
 if(FORGE_ENABLE_WERROR)
  target_compile_options(forge PRIVATE /WX)
 endif()
else()
 target_compile_options(forge PRIVATE -Wall -Wextra -Wpedantic)
 if(FORGE_ENABLE_WERROR)
  target_compile_options(forge PRIVATE -Werror)
 endif()
endif()
function(forge_apply_warnings target)
 if(MSVC)
  target_compile_options(${target} PRIVATE /W4 /permissive-)
  if(FORGE_ENABLE_WERROR)
   target_compile_options(${target} PRIVATE /WX)
  endif()
 else()
  target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  if(FORGE_ENABLE_WERROR)
   target_compile_options(${target} PRIVATE -Werror)
  endif()
 endif()
endfunction()
function(forge_add_tool name source)
 add_executable(${name} ${source})
 target_link_libraries(${name} PRIVATE Forge::forge)
 forge_apply_warnings(${name})
endfunction()
if(FORGE_BUILD_TOOLS)
 forge_add_tool(forge-cli tools/forge/main.cpp)
 set_target_properties(forge-cli PROPERTIES OUTPUT_NAME forge)
 forge_add_tool(forge-as tools/forge-as/main.cpp)
 forge_add_tool(forge-dis tools/forge-dis/main.cpp)
 forge_add_tool(forge-opt tools/forge-opt/main.cpp)
 forge_add_tool(forge-codegen tools/forge-codegen/main.cpp)
 forge_add_tool(forge-run tools/forge-run/main.cpp)
endif()
if(FORGE_BUILD_TESTS)
 enable_testing()
 function(forge_add_test name source)
  add_executable(${name} ${source})
  target_link_libraries(${name} PRIVATE Forge::forge)
  forge_apply_warnings(${name})
  add_test(NAME ${name} COMMAND ${name})
 endfunction()
 forge_add_test(forge-diagnostic-tests tests/diagnostics/diagnostic_tests.cpp)
 forge_add_test(forge-ir-tests tests/ir/ir_tests.cpp)
 forge_add_test(forge-signature-metadata-tests tests/ir/signature_metadata_tests.cpp)
 forge_add_test(forge-builder-tests tests/frontend/builder_tests.cpp)
 forge_add_test(forge-frontend-kit-tests tests/frontend/frontend_kit_tests.cpp)
 add_executable(forge-minilang-example
  examples/frontend/minilang/src/main.cpp
  examples/frontend/minilang/src/lexer.cpp
  examples/frontend/minilang/src/parser.cpp
  examples/frontend/minilang/src/codegen.cpp)
 target_include_directories(forge-minilang-example PRIVATE examples/frontend/minilang/include)
 target_link_libraries(forge-minilang-example PRIVATE Forge::forge)
 forge_apply_warnings(forge-minilang-example)
 add_test(NAME forge-minilang-frontend-example
  COMMAND forge-minilang-example ${CMAKE_CURRENT_SOURCE_DIR}/examples/frontend/minilang/example.mini main --source-map)
 forge_add_test(forge-c-api-tests tests/frontend/c_api_tests.cpp)
 forge_add_test(forge-incremental-cache-tests tests/frontend/incremental_cache_tests.cpp)
 forge_add_test(forge-build-driver-tests tests/frontend/build_driver_tests.cpp)
forge_add_test(forge-dependency-build-tests tests/frontend/dependency_build_tests.cpp)
 forge_add_test(forge-analysis-tests tests/analysis/analysis_tests.cpp)
 forge_add_test(forge-binary-tests tests/serialization/binary_tests.cpp)
 forge_add_test(forge-codegen-tests tests/codegen/codegen_tests.cpp)
 forge_add_test(forge-abi-hardening-tests tests/codegen/abi_hardening_tests.cpp)
 forge_add_test(forge-interpreter-tests tests/interpreter/interpreter_tests.cpp)
 forge_add_test(forge-float-interpreter-tests tests/interpreter/float_tests.cpp)
 forge_add_test(forge-optimization-tests tests/analysis/optimization_tests.cpp)
 forge_add_test(forge-differential-tests tests/differential/differential_tests.cpp)
 forge_add_test(forge-jit-invoke-tests tests/jit/invoke_tests.cpp)
 forge_add_test(forge-runtime-binding-tests tests/runtime/binding_tests.cpp)
 forge_add_test(forge-data-layout-tests tests/target/data_layout_tests.cpp)
 forge_add_test(forge-abi-classification-tests tests/target/abi_classification_tests.cpp)
 forge_add_test(forge-object-tests tests/object/elf_tests.cpp)
 forge_add_test(forge-coff-object-tests tests/object/coff_tests.cpp)
 forge_add_test(forge-archive-tests tests/object/archive_tests.cpp)
forge_add_test(forge-incremental-object-tests tests/object/incremental_object_tests.cpp)
 forge_add_test(forge-native-incremental-link-tests tests/object/native_incremental_link_tests.cpp)
 add_test(NAME forge-elf-deterministic-output COMMAND ${CMAKE_COMMAND}
  -DFORGE=$<TARGET_FILE:forge-cli> -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/examples/native-i64.fir -DFORMAT=elf
  -DOUTPUT1=${CMAKE_CURRENT_BINARY_DIR}/deterministic-a.o -DOUTPUT2=${CMAKE_CURRENT_BINARY_DIR}/deterministic-b.o
  -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/object/check_deterministic.cmake)
 add_test(NAME forge-coff-deterministic-output COMMAND ${CMAKE_COMMAND}
  -DFORGE=$<TARGET_FILE:forge-cli> -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/examples/native-i64.fir -DFORMAT=coff
  -DOUTPUT1=${CMAKE_CURRENT_BINARY_DIR}/deterministic-a.obj -DOUTPUT2=${CMAKE_CURRENT_BINARY_DIR}/deterministic-b.obj
  -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/object/check_deterministic.cmake)
 add_test(NAME forge-optimization-levels-release-gate COMMAND ${CMAKE_COMMAND}
  -DFORGE_OPT=$<TARGET_FILE:forge-opt> -DFORGE=$<TARGET_FILE:forge-cli>
  -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/examples/optimization.fir
  -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/optimization-levels
  -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/diagnostics/check_optimization_levels.cmake)
 add_test(NAME forge-example-corpus-release-gate COMMAND ${CMAKE_COMMAND}
  -DFORGE=$<TARGET_FILE:forge-cli> -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
  -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/diagnostics/check_example_corpus.cmake)
 add_test(NAME forge-repository-hygiene-release-gate COMMAND ${CMAKE_COMMAND}
  -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
  -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/diagnostics/check_repository_hygiene.cmake)
 add_test(NAME forge-install-consumer-release-gate COMMAND ${CMAKE_COMMAND}
  -DBUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
  -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
  -DCMAKE_COMMAND_PATH=${CMAKE_COMMAND}
  -DPARENT_GENERATOR=${CMAKE_GENERATOR}
  -DPARENT_GENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}
  -DPARENT_GENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}
  -DPARENT_C_COMPILER=${CMAKE_C_COMPILER}
  -DPARENT_CXX_COMPILER=${CMAKE_CXX_COMPILER}
  -DPARENT_BUILD_TYPE=${CMAKE_BUILD_TYPE}
  -DPARENT_CONFIG=$<CONFIG>
  -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/package/check_install_consumer.cmake)
 if(UNIX AND NOT APPLE)
  add_test(NAME forge-static-library-workflow COMMAND ${CMAKE_COMMAND}
   -DFORGE=$<TARGET_FILE:forge-cli> -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/examples/library-answer.fir
   -DOBJECT=${CMAKE_CURRENT_BINARY_DIR}/forge-static-library.o
   -DARCHIVE=${CMAKE_CURRENT_BINARY_DIR}/libforge-answer.a
   -DSOURCE=${CMAKE_CURRENT_BINARY_DIR}/forge-static-consumer.cpp
   -DEXECUTABLE=${CMAKE_CURRENT_BINARY_DIR}/forge-static-consumer
   -DCXX_COMPILER=${CMAKE_CXX_COMPILER}
   -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/object/check_static_library.cmake)
  add_test(NAME forge-shared-library-workflow COMMAND ${CMAKE_COMMAND}
   -DFORGE=$<TARGET_FILE:forge-cli> -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/examples/library-answer.fir
   -DOBJECT=${CMAKE_CURRENT_BINARY_DIR}/forge-shared-library.o
   -DLIBRARY=${CMAKE_CURRENT_BINARY_DIR}/libforge-answer.so
   -DSOURCE=${CMAKE_CURRENT_BINARY_DIR}/forge-shared-consumer.cpp
   -DEXECUTABLE=${CMAKE_CURRENT_BINARY_DIR}/forge-shared-consumer
   -DCXX_COMPILER=${CMAKE_CXX_COMPILER}
   -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/object/check_shared_library.cmake)
  add_test(NAME forge-native-link-workflow COMMAND ${CMAKE_COMMAND}
   -DFORGE=$<TARGET_FILE:forge-cli> -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/examples/link.fir
   -DOBJECT=${CMAKE_CURRENT_BINARY_DIR}/forge-link-test.o -DEXECUTABLE=${CMAKE_CURRENT_BINARY_DIR}/forge-link-test
   -DC_COMPILER=${CMAKE_CXX_COMPILER} -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/object/check_native_link.cmake)
 endif()
 add_test(NAME forge-code-quality-baseline COMMAND ${CMAKE_COMMAND}
  -DFORGE_CODEGEN=$<TARGET_FILE:forge-codegen>
  -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/examples/optimization.fir
  -DMAX_ENCODED_BYTES=125
  -DMIN_DEFERRED_STORES=0
  -DMIN_REMATERIALIZED_VALUES=0
  -DMIN_PRESERVED_INSTRUCTIONS=1
  -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/codegen/check_quality.cmake)
 add_test(NAME forge-machine-cleanup-baseline COMMAND ${CMAKE_COMMAND}
  -DFORGE_CODEGEN=$<TARGET_FILE:forge-codegen>
  -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/examples/machine-cleanup.fir
  -DMAX_ENCODED_BYTES=11
  -DMIN_COPIES_PROPAGATED=3
  -DMIN_LEAF_FRAMES_ELIDED=1
  -DMIN_LEAF_FRAME_BYTES_AVOIDED=5
  -DMIN_INSTRUCTIONS_ELIMINATED=3
  -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/codegen/check_machine_cleanup.cmake)
 add_test(NAME forge-address-mode-baseline COMMAND ${CMAKE_COMMAND}
  -DFORGE_CODEGEN=$<TARGET_FILE:forge-codegen>
  -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/examples/address-mode.fir
  -DMAX_ENCODED_BYTES=33
  -DMIN_ADDRESS_MODES_FOLDED=1
  -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/codegen/check_address_mode.cmake)
 add_test(NAME forge-compare-branch-baseline COMMAND ${CMAKE_COMMAND}
  -DFORGE_CODEGEN=$<TARGET_FILE:forge-codegen>
  -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/examples/compare-branch.fir
  -DMAX_ENCODED_BYTES=36
  -DMIN_COMPARE_BRANCHES_FUSED=1
  -DMIN_SHORT_CONDITIONAL_BRANCHES=1
  -DMIN_SHORT_CONDITIONAL_BYTES_AVOIDED=4
  -DMIN_FIXED_BYTES_AVOIDED=8
  -DMIN_BLOCKS_REORDERED=1
  -DMIN_FALLTHROUGH_JUMPS=1
  -DMIN_BRANCHES_INVERTED=1
  -DMIN_LAYOUT_BYTES_AVOIDED=4
  -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/codegen/check_compare_branch.cmake)
 add_test(NAME forge-short-branch-baseline COMMAND ${CMAKE_COMMAND}
  -DFORGE_CODEGEN=$<TARGET_FILE:forge-codegen>
  -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/examples/short-branch.fir
  -DMAX_ENCODED_BYTES=2
  -DMIN_SHORT_BRANCHES=1
  -DMIN_FORWARD_SHORT_BRANCHES=0
  -DMIN_BYTES_AVOIDED=3
  -DMIN_FALLTHROUGH_JUMPS=1
  -DMIN_LAYOUT_BYTES_AVOIDED=2
  -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/codegen/check_short_branch.cmake)
 add_test(NAME forge-cfg-cleanup-baseline COMMAND ${CMAKE_COMMAND}
  -DFORGE_CODEGEN=$<TARGET_FILE:forge-codegen>
  -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/examples/cfg-cleanup.fir
  -DMAX_ENCODED_BYTES=18
  -DMIN_JUMP_THREADS=1
  -DMIN_EMPTY_BLOCKS_REMOVED=1
  -DMIN_UNREACHABLE_BLOCKS_REMOVED=1
  -DMIN_INSTRUCTIONS_ELIMINATED=3
  -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/codegen/check_cfg_cleanup.cmake)
 add_test(NAME forge-immediate-forms-baseline COMMAND ${CMAKE_COMMAND}
  -DFORGE_CODEGEN=$<TARGET_FILE:forge-codegen>
  -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/examples/immediate-forms.fir
  -DMAX_ENCODED_BYTES=55
  -DMIN_IMMEDIATE_FORMS=4
  -DMIN_CONSTANTS_ELIMINATED=4
  -DMIN_INSTRUCTIONS_ELIMINATED=4
  -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/codegen/check_immediate_forms.cmake)
 add_test(NAME forge-constant-store-baseline COMMAND ${CMAKE_COMMAND}
  -DFORGE_CODEGEN=$<TARGET_FILE:forge-codegen>
  -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/examples/constant-store.fir
  -DMAX_ENCODED_BYTES=31
  -DMIN_CONSTANT_STORES=1
  -DMIN_CONSTANTS_ELIMINATED=1
  -DMIN_INSTRUCTIONS_ELIMINATED=2
  -DMIN_LOAD_RETURNS_FOLDED=1
  -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/codegen/check_constant_store.cmake)
 add_test(NAME forge-memory-arithmetic-baseline COMMAND ${CMAKE_COMMAND}
  -DFORGE_CODEGEN=$<TARGET_FILE:forge-codegen>
  -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/examples/memory-arithmetic.fir
  -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/codegen/check_memory_arithmetic.cmake)
 add_test(NAME forge-immediate-control-baseline COMMAND ${CMAKE_COMMAND}
  -DFORGE_CODEGEN=$<TARGET_FILE:forge-codegen>
  -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/examples/immediate-control.fir
  -DMAX_ENCODED_BYTES=27
  -DMIN_IMMEDIATE_COMPARISONS=1
  -DMIN_DIRECT_RETURNS=2
  -DMIN_ZEROING_IDIOMS=1
  -DMIN_CONSTANTS_ELIMINATED=3
  -DMIN_INSTRUCTIONS_ELIMINATED=4
  -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/codegen/check_immediate_control.cmake)
 add_test(NAME forge-extension-cleanup-baseline COMMAND ${CMAKE_COMMAND}
  -DCODEGEN=$<TARGET_FILE:forge-codegen>
  -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/examples/extension-cleanup.fir
  -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/codegen/check_extension_cleanup.cmake)
 add_test(NAME forge-machine-liveness-baseline COMMAND ${CMAKE_COMMAND}
  -DCODEGEN=$<TARGET_FILE:forge-codegen>
  -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/examples/machine-liveness.fir
  -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/codegen/check_machine_liveness.cmake)
 add_test(NAME forge-floating-compare-baseline COMMAND ${CMAKE_COMMAND}
  -DFORGE_CODEGEN=$<TARGET_FILE:forge-codegen>
  -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/examples/floating-compare.fir
  -DMIN_FUSED=1
  -DMIN_BYTES_AVOIDED=8
  -DMAX_ENCODED_BYTES=60
  -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/codegen/check_floating_compare.cmake)
 add_test(NAME forge-driver-compile COMMAND forge-cli compile ${CMAKE_CURRENT_SOURCE_DIR}/examples/native-i64.fir -o ${CMAKE_CURRENT_BINARY_DIR}/forge-driver-test.o)
 if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  set_tests_properties(forge-driver-compile PROPERTIES PASS_REGULAR_EXPRESSION "FORGE  emitted COFF AMD64 object")
 else()
  set_tests_properties(forge-driver-compile PROPERTIES PASS_REGULAR_EXPRESSION "FORGE  emitted ELF64 object")
 endif()
 add_test(NAME forge-driver-compile-coff COMMAND forge-cli compile ${CMAKE_CURRENT_SOURCE_DIR}/examples/native-i64.fir --format=coff -o ${CMAKE_CURRENT_BINARY_DIR}/forge-driver-test.obj)
 set_tests_properties(forge-driver-compile-coff PROPERTIES PASS_REGULAR_EXPRESSION "FORGE  emitted COFF AMD64 object")
 add_test(NAME forge-driver-version COMMAND forge-cli version)
 set_tests_properties(forge-driver-version PROPERTIES PASS_REGULAR_EXPRESSION "forge 2.0.0")
 add_test(NAME forge-driver-doctor COMMAND forge-cli doctor)
 set_tests_properties(forge-driver-doctor PROPERTIES PASS_REGULAR_EXPRESSION "FORGE  doctor passed")
 add_test(NAME forge-driver-inspect COMMAND forge-cli inspect ${CMAKE_CURRENT_SOURCE_DIR}/examples/native-i64.fir --stage=all)
 set_tests_properties(forge-driver-inspect PROPERTIES PASS_REGULAR_EXPRESSION "=== MACHINE IR ===")
 add_test(NAME forge-driver-explain COMMAND forge-cli explain ${CMAKE_CURRENT_SOURCE_DIR}/examples/optimization.fir -O2)
 set_tests_properties(forge-driver-explain PROPERTIES PASS_REGULAR_EXPRESSION "TOTAL  rewritten=")
 add_test(NAME forge-driver-verify COMMAND forge-cli verify ${CMAKE_CURRENT_SOURCE_DIR}/examples/native-i64.fir)
 set_tests_properties(forge-driver-verify PROPERTIES PASS_REGULAR_EXPRESSION "FORGE  verified")
 add_test(NAME forge-driver-source-diagnostic COMMAND ${CMAKE_COMMAND}
  -DFORGE_EXE=$<TARGET_FILE:forge-cli>
  -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/diagnostics/invalid-source.fir
  -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/diagnostics/check_cli.cmake)
 add_test(NAME forge-run-interpreter-cli
  COMMAND forge-run --engine=interpreter ${CMAKE_CURRENT_SOURCE_DIR}/examples/native-i64.fir wide_math 9 4)
 set_tests_properties(forge-run-interpreter-cli PROPERTIES PASS_REGULAR_EXPRESSION "39")
 if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
  add_test(NAME forge-run-jit-cli
   COMMAND forge-run --engine=jit ${CMAKE_CURRENT_SOURCE_DIR}/examples/native-i64.fir wide_math 9 4)
  set_tests_properties(forge-run-jit-cli PROPERTIES PASS_REGULAR_EXPRESSION "39")
  add_test(NAME forge-run-compare-cli
   COMMAND forge-run --engine=compare ${CMAKE_CURRENT_SOURCE_DIR}/examples/interpreter.fir factorial 10)
  set_tests_properties(forge-run-compare-cli PROPERTIES PASS_REGULAR_EXPRESSION "3628800")
  add_test(NAME forge-run-constant-store
   COMMAND forge-run --engine=compare ${CMAKE_CURRENT_SOURCE_DIR}/examples/constant-store.fir store_constant)
  set_tests_properties(forge-run-constant-store PROPERTIES PASS_REGULAR_EXPRESSION "42")
  add_test(NAME forge-run-immediate-control-low
   COMMAND forge-run --engine=compare ${CMAKE_CURRENT_SOURCE_DIR}/examples/immediate-control.fir classify 3)
  set_tests_properties(forge-run-immediate-control-low PROPERTIES PASS_REGULAR_EXPRESSION "0")
  add_test(NAME forge-run-immediate-control-high
   COMMAND forge-run --engine=compare ${CMAKE_CURRENT_SOURCE_DIR}/examples/immediate-control.fir classify 9)
  set_tests_properties(forge-run-immediate-control-high PROPERTIES PASS_REGULAR_EXPRESSION "1")
  add_test(NAME forge-run-eight-argument-cli
   COMMAND forge-run --engine=compare ${CMAKE_CURRENT_SOURCE_DIR}/examples/abi.fir sum_eight 1 2 3 4 5 6 7 8)
  set_tests_properties(forge-run-eight-argument-cli PROPERTIES PASS_REGULAR_EXPRESSION "36")
  set_tests_properties(forge-run-jit-cli forge-run-compare-cli forge-run-constant-store forge-run-immediate-control-low forge-run-immediate-control-high forge-run-eight-argument-cli PROPERTIES LABELS "jit-runtime")
 endif()
 set_tests_properties(forge-jit-invoke-tests forge-runtime-binding-tests forge-differential-tests PROPERTIES LABELS "jit-runtime")
 set_tests_properties(forge-install-consumer-release-gate PROPERTIES LABELS "installed-consumer")
endif()
if(FORGE_BUILD_FUZZERS)
 forge_add_test(forge-parser-fuzz-smoke tests/fuzz/parser_fuzz_smoke.cpp)
 forge_add_test(forge-binary-fuzz-smoke tests/fuzz/binary_fuzz_smoke.cpp)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
install(TARGETS forge EXPORT ForgeTargets
 ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
 LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
 RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
 INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
if(FORGE_BUILD_TOOLS)
 install(TARGETS forge-cli forge-as forge-dis forge-opt forge-codegen forge-run RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} PATTERN "*.in" EXCLUDE)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/generated/forge/version.hpp DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/forge)
write_basic_package_version_file(
 ${CMAKE_CURRENT_BINARY_DIR}/ForgeConfigVersion.cmake
 VERSION ${PROJECT_VERSION}
 COMPATIBILITY SameMajorVersion)
configure_package_config_file(
 ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ForgeConfig.cmake.in
 ${CMAKE_CURRENT_BINARY_DIR}/ForgeConfig.cmake
 INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Forge)
install(FILES
 ${CMAKE_CURRENT_BINARY_DIR}/ForgeConfig.cmake
 ${CMAKE_CURRENT_BINARY_DIR}/ForgeConfigVersion.cmake
 DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Forge)
install(EXPORT ForgeTargets
 FILE ForgeTargets.cmake
 NAMESPACE Forge::
 DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Forge)

if(FORGE_INSTALL_EXAMPLES)
 install(DIRECTORY examples/ DESTINATION ${CMAKE_INSTALL_DATADIR}/forge/examples
  FILES_MATCHING PATTERN "*.fir")
endif()
if(FORGE_INSTALL_DOCS)
 install(FILES README.md CHANGELOG.md CONTRIBUTING.md SECURITY.md LICENSE
  DESTINATION ${CMAKE_INSTALL_DOCDIR})
 install(DIRECTORY docs/ DESTINATION ${CMAKE_INSTALL_DOCDIR})
endif()

set(CPACK_PACKAGE_NAME "Forge")
set(CPACK_PACKAGE_VENDOR "Forge Compiler Project")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "forge-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
if(WIN32)
 set(CPACK_GENERATOR "ZIP")
else()
 set(CPACK_GENERATOR "TGZ")
endif()
set(CPACK_SOURCE_GENERATOR "TGZ")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
set(CPACK_SOURCE_PACKAGE_FILE_NAME "forge-${PROJECT_VERSION}-source")
set(CPACK_SOURCE_IGNORE_FILES
 "/build/"
 "/_packages/"
 "/\.git/"
 "/\.github/"
 "/\.idea/"
 "/\.vscode/"
 "~$"
 "\.swp$"
 "\.zip$"
 "\.tar\.gz$")
include(CPack)
