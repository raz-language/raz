# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

# Python powers source-audit and stdlib-closure fixtures. Resolve it here so
# a normal CMake configure does not silently register empty test commands.
if(NOT Python3_EXECUTABLE)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
endif()

add_executable(raz-frontend-tests ${PROJECT_SOURCE_DIR}/tests/native/frontend/frontend_tests.cpp)
target_link_libraries(raz-frontend-tests PRIVATE Raz::bootstrap_compiler)
add_test(NAME raz-frontend-tests COMMAND raz-frontend-tests)

add_executable(raz-parser-tests ${PROJECT_SOURCE_DIR}/tests/native/frontend/parser_tests.cpp)
target_link_libraries(raz-parser-tests PRIVATE Raz::bootstrap_compiler)
add_test(NAME raz-parser-tests COMMAND raz-parser-tests)

add_test(NAME razc-help COMMAND razc_host --help)
add_test(NAME razc-lex-example COMMAND razc_host ${PROJECT_SOURCE_DIR}/tests/examples/basics/hello.rz)
add_test(NAME razc-token-dump COMMAND razc_host --tokens ${PROJECT_SOURCE_DIR}/tests/examples/basics/hello.rz)

add_test(NAME razc-ast-dump COMMAND razc_host --ast ${PROJECT_SOURCE_DIR}/tests/examples/basics/hello.rz)

add_test(NAME razc-reject-unsupported-extension COMMAND razc_host ${PROJECT_SOURCE_DIR}/tests/fixtures/unsupported_extension.txt)
set_tests_properties(razc-reject-unsupported-extension PROPERTIES WILL_FAIL TRUE)

add_executable(raz-semantic-tests ${PROJECT_SOURCE_DIR}/tests/native/semantic/semantic_tests.cpp)
target_link_libraries(raz-semantic-tests PRIVATE Raz::bootstrap_compiler)
add_test(NAME raz-semantic-tests COMMAND raz-semantic-tests)

add_test(NAME razc-hir-dump COMMAND razc_host --hir ${PROJECT_SOURCE_DIR}/tests/examples/basics/hello.rz)

add_executable(raz-mir-tests ${PROJECT_SOURCE_DIR}/tests/native/ir/mir_tests.cpp)
target_link_libraries(raz-mir-tests PRIVATE Raz::bootstrap_compiler)
add_test(NAME raz-mir-tests COMMAND raz-mir-tests)

add_test(NAME razc-mir-dump COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/basics/arithmetic.rz)
add_test(NAME razc-forge-ir-dump COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/basics/arithmetic.rz)

add_test(NAME razc-mir-control-flow COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/basics/control_flow.rz)
add_test(NAME razc-forge-control-flow COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/basics/control_flow.rz)

add_test(NAME razc-mir-mutable-places COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/basics/mutable_places.rz)
add_test(NAME razc-forge-mutable-places COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/basics/mutable_places.rz)

add_test(NAME razc-mir-operators COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/basics/operators.rz)
add_test(NAME razc-forge-operators COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/basics/operators.rz)
add_test(NAME razc-check-numeric-casts COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/basics/numeric_casts.rz)
add_test(NAME razc-mir-numeric-casts COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/basics/numeric_casts.rz)
set_tests_properties(razc-mir-numeric-casts PROPERTIES PASS_REGULAR_EXPRESSION "cast.numeric")
add_test(NAME razc-forge-numeric-casts COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/basics/numeric_casts.rz)
set_tests_properties(razc-forge-numeric-casts PROPERTIES PASS_REGULAR_EXPRESSION "int_to_float.signed f64;float_to_int.signed i64;truncate i32;int_to_float.unsigned f64;float_to_int.unsigned i32;zero_extend i64;int_to_float.signed f32;float_extend f64;float_truncate f32")
add_test(NAME razc-reject-implicit-mixed-numeric COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/basics/invalid_mixed_numeric.rz)
set_tests_properties(razc-reject-implicit-mixed-numeric PROPERTIES WILL_FAIL TRUE)
add_test(NAME razc-check-integer-widths COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/basics/integer_widths.rz)
add_test(NAME razc-mir-integer-widths COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/basics/integer_widths.rz)
set_tests_properties(razc-mir-integer-widths PROPERTIES PASS_REGULAR_EXPRESSION "cast.numeric u8;cast.numeric u16")
add_test(NAME razc-forge-integer-widths COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/basics/integer_widths.rz)
set_tests_properties(razc-forge-integer-widths PROPERTIES PASS_REGULAR_EXPRESSION "truncate i8;zero_extend i16;add i8")

add_test(NAME razc-hir-struct-layout COMMAND razc_host --hir ${PROJECT_SOURCE_DIR}/tests/examples/aggregates/structs.rz)
add_test(NAME razc-mir-struct-fields COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/aggregates/structs.rz)
add_test(NAME razc-forge-struct-fields COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/aggregates/structs.rz)

add_test(NAME razc-hir-nested-array-layout COMMAND razc_host --hir ${PROJECT_SOURCE_DIR}/tests/examples/aggregates/arrays_nested.rz)
add_test(NAME razc-mir-array-indexing COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/aggregates/arrays_nested.rz)
add_test(NAME razc-forge-array-indexing COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/aggregates/arrays_nested.rz)
add_test(NAME razc-reject-constant-array-oob COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/aggregates/out_of_bounds.rz)
set_tests_properties(razc-reject-constant-array-oob PROPERTIES WILL_FAIL TRUE)

add_test(NAME razc-hir-array-initializers COMMAND razc_host --hir ${PROJECT_SOURCE_DIR}/tests/examples/aggregates/array_safety_copy.rz)
add_test(NAME razc-mir-array-bounds-copy COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/aggregates/array_safety_copy.rz)
add_test(NAME razc-forge-array-bounds-copy COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/aggregates/array_safety_copy.rz)
add_test(NAME razc-reject-array-initializer-count COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/aggregates/invalid_array_initializer_count.rz)
set_tests_properties(razc-reject-array-initializer-count PROPERTIES WILL_FAIL TRUE)
add_test(NAME razc-reject-array-initializer-type COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/aggregates/invalid_array_initializer_type.rz)
set_tests_properties(razc-reject-array-initializer-type PROPERTIES WILL_FAIL TRUE)

add_test(NAME razc-hir-enums COMMAND razc_host --hir ${PROJECT_SOURCE_DIR}/tests/examples/basics/enums_match.rz)
add_test(NAME razc-mir-enum-match COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/basics/enums_match.rz)
add_test(NAME razc-forge-enum-match COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/basics/enums_match.rz)
add_test(NAME razc-enum-match-wildcard COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/enums/wildcard.rz)
add_test(NAME razc-reject-non-exhaustive-match COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/enums/non_exhaustive.rz)
set_tests_properties(razc-reject-non-exhaustive-match PROPERTIES WILL_FAIL TRUE)

add_test(NAME razc-hir-generic-payload-types COMMAND razc_host --hir ${PROJECT_SOURCE_DIR}/tests/examples/generics/option_result.rz)
add_test(NAME razc-check-generic-instantiations COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/generics/instantiations.rz)
add_test(NAME razc-check-library-option COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/library/core/option/option.rz)
add_test(NAME razc-check-library-result COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/library/core/result/result.rz)
add_test(NAME razc-reject-generic-arity COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/generics/invalid_arity.rz)
set_tests_properties(razc-reject-generic-arity PROPERTIES WILL_FAIL TRUE)
add_test(NAME razc-reject-unknown-payload-type COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/generics/invalid_payload.rz)
set_tests_properties(razc-reject-unknown-payload-type PROPERTIES WILL_FAIL TRUE)

add_test(NAME razc-hir-concrete-generic-layouts COMMAND razc_host --hir ${PROJECT_SOURCE_DIR}/tests/examples/generics/concrete_layouts.rz)
set_tests_properties(razc-hir-concrete-generic-layouts PROPERTIES PASS_REGULAR_EXPRESSION "type @Pair<i64,bool> \\[instantiated\\] size 16 align 8;enum @Option<i64> \\[instantiated\\] repr tagged size 16 align 8")
add_test(NAME razc-check-payload-constructors COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/enums/payload_constructors.rz)
add_test(NAME razc-check-nested-payload-constructor COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/enums/nested_payload_constructor.rz)
add_test(NAME razc-mir-nested-payload-constructor COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/enums/nested_payload_constructor.rz)
add_test(NAME razc-reject-invalid-payload-constructor COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/enums/invalid_payload_constructor.rz)
set_tests_properties(razc-reject-invalid-payload-constructor PROPERTIES WILL_FAIL TRUE)

add_test(NAME razc-check-payload-match COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/enums/payload_match.rz)
add_test(NAME razc-mir-payload-match COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/enums/payload_match.rz)
set_tests_properties(razc-mir-payload-match PROPERTIES PASS_REGULAR_EXPRESSION "stack.alloc Message 24 8;ptr.offset i32;ptr.offset i64;load i64")
add_test(NAME razc-forge-payload-match COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/enums/payload_match.rz)
set_tests_properties(razc-forge-payload-match PROPERTIES PASS_REGULAR_EXPRESSION "func @evaluate\(%message: ptr\) -> i64;load i32;load i64")
add_test(NAME razc-reject-payload-pattern-count COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/enums/invalid_payload_pattern_count.rz)
set_tests_properties(razc-reject-payload-pattern-count PROPERTIES WILL_FAIL TRUE)

add_test(NAME razc-hir-generic-functions COMMAND razc_host --hir ${PROJECT_SOURCE_DIR}/tests/examples/generics/functions.rz)
set_tests_properties(razc-hir-generic-functions PROPERTIES PASS_REGULAR_EXPRESSION "fn @identity<i64> \\[instantiated\\].*i64 %value.*-> i64")
add_test(NAME razc-mir-generic-functions COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/generics/functions.rz)
set_tests_properties(razc-mir-generic-functions PROPERTIES PASS_REGULAR_EXPRESSION "call i64 identity<i64>;fn @identity<i64>\\(i64 %value\\) -> i64")
add_test(NAME razc-forge-generic-functions COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/generics/functions.rz)
set_tests_properties(razc-forge-generic-functions PROPERTIES PASS_REGULAR_EXPRESSION "call i64 @identity<i64>;func @identity<i64>\\(%value: i64\\) -> i64")
add_test(NAME razc-reject-generic-function-missing-arguments COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/generics/missing_function_arguments.rz)
set_tests_properties(razc-reject-generic-function-missing-arguments PROPERTIES WILL_FAIL TRUE)
add_test(NAME razc-reject-generic-function-type-mismatch COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/generics/function_type_mismatch.rz)
set_tests_properties(razc-reject-generic-function-type-mismatch PROPERTIES WILL_FAIL TRUE)

add_test(NAME razc-check-error-propagation COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/generics/error_propagation.rz)
add_test(NAME razc-mir-error-propagation COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/generics/error_propagation.rz)
set_tests_properties(razc-mir-error-propagation PROPERTIES PASS_REGULAR_EXPRESSION "try.success;try.failure;return Result<i64,i64>;return Option<i64>")
add_test(NAME razc-forge-error-propagation COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/generics/error_propagation.rz)
set_tests_properties(razc-forge-error-propagation PROPERTIES PASS_REGULAR_EXPRESSION "func @relay;func @unwrap_maybe;branch")
add_test(NAME razc-reject-invalid-try-return COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/generics/invalid_try_return.rz)
set_tests_properties(razc-reject-invalid-try-return PROPERTIES WILL_FAIL TRUE)

add_test(NAME razc-check-defer COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/basics/defer.rz)
add_test(NAME razc-mir-defer COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/basics/defer.rz)
set_tests_properties(razc-mir-defer PROPERTIES PASS_REGULAR_EXPRESSION "mul i64;add i64;return i64")
add_test(NAME razc-forge-defer COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/basics/defer.rz)
set_tests_properties(razc-forge-defer PROPERTIES PASS_REGULAR_EXPRESSION "func @cleanup;func @loop_cleanup")
add_test(NAME razc-reject-defer-control-transfer COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/basics/invalid_defer_control.rz)
set_tests_properties(razc-reject-defer-control-transfer PROPERTIES WILL_FAIL TRUE)
add_test(NAME razc-check-nested-defer COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/basics/nested_defer.rz)
add_test(NAME razc-mir-nested-defer COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/basics/nested_defer.rz)
set_tests_properties(razc-mir-nested-defer PROPERTIES PASS_REGULAR_EXPRESSION "mul i64;add i64;return i64")
add_test(NAME razc-forge-nested-defer COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/basics/nested_defer.rz)
set_tests_properties(razc-forge-nested-defer PROPERTIES PASS_REGULAR_EXPRESSION "func @nested_cleanup")

# Soundness / non-lexical lifetime qualification. These fixtures intentionally
# separate safe NLL behavior from programs that must remain impossible in safe Raz.
add_test(NAME razc-soundness-nll-unused-borrow
  COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/soundness/nll_unused_borrow.rz)
add_test(NAME razc-soundness-nll-last-use
  COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/soundness/nll_last_use.rz)
add_test(NAME razc-soundness-reject-aggregate-reference-escape
  COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/soundness/invalid_aggregate_reference_escape.rz)
set_tests_properties(razc-soundness-reject-aggregate-reference-escape PROPERTIES WILL_FAIL TRUE)
add_test(NAME razc-soundness-reject-aggregate-reference-escape-via-local
  COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/soundness/invalid_aggregate_reference_escape_via_local.rz)
set_tests_properties(razc-soundness-reject-aggregate-reference-escape-via-local PROPERTIES WILL_FAIL TRUE)
add_test(NAME razc-soundness-reject-reference-rebind
  COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/soundness/invalid_reference_rebind.rz)
set_tests_properties(razc-soundness-reject-reference-rebind PROPERTIES WILL_FAIL TRUE)
add_test(NAME razc-soundness-reject-slice-rebind
  COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/soundness/invalid_slice_rebind.rz)
set_tests_properties(razc-soundness-reject-slice-rebind PROPERTIES WILL_FAIL TRUE)

add_test(NAME razc-check-explicit-moves COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/ownership/moves.rz)
add_test(NAME razc-mir-explicit-moves COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/ownership/moves.rz)
add_test(NAME razc-forge-explicit-moves COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/ownership/moves.rz)
add_test(NAME razc-reject-use-after-move COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/ownership/use_after_move.rz)
set_tests_properties(razc-reject-use-after-move PROPERTIES WILL_FAIL TRUE)

add_test(NAME razc-check-references COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/ownership/references.rz)
add_test(NAME razc-mir-references COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/ownership/references.rz)
set_tests_properties(razc-mir-references PROPERTIES PASS_REGULAR_EXPRESSION "fn @increment.*i64&mut %value;load i64&mut;store i64")
add_test(NAME razc-forge-references COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/ownership/references.rz)
set_tests_properties(razc-forge-references PROPERTIES PASS_REGULAR_EXPRESSION "func @increment\(%value: ptr\) -> i64;load ptr;store i64")
add_test(NAME razc-reject-double-mutable-borrow COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/ownership/invalid_double_mutable_borrow.rz)
set_tests_properties(razc-reject-double-mutable-borrow PROPERTIES WILL_FAIL TRUE)
add_test(NAME razc-reject-move-while-borrowed COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/ownership/invalid_move_while_borrowed.rz)
set_tests_properties(razc-reject-move-while-borrowed PROPERTIES WILL_FAIL TRUE)
add_test(NAME razc-reject-immutable-reference-write COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/ownership/invalid_immutable_reference_write.rz)
set_tests_properties(razc-reject-immutable-reference-write PROPERTIES WILL_FAIL TRUE)

add_test(NAME razc-check-field-sensitive-borrows COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/ownership/field_borrows.rz)
add_test(NAME razc-mir-field-sensitive-borrows COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/ownership/field_borrows.rz)
set_tests_properties(razc-mir-field-sensitive-borrows PROPERTIES PASS_REGULAR_EXPRESSION "ptr.offset i64;store i64")
add_test(NAME razc-forge-field-sensitive-borrows COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/ownership/field_borrows.rz)
set_tests_properties(razc-forge-field-sensitive-borrows PROPERTIES PASS_REGULAR_EXPRESSION "ptr.offset ptr;store i64")
add_test(NAME razc-reject-overlapping-field-borrow COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/ownership/invalid_overlapping_field_borrow.rz)
set_tests_properties(razc-reject-overlapping-field-borrow PROPERTIES WILL_FAIL TRUE)
add_test(NAME razc-reject-whole-field-borrow COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/ownership/invalid_whole_field_borrow.rz)
set_tests_properties(razc-reject-whole-field-borrow PROPERTIES WILL_FAIL TRUE)
add_test(NAME razc-reject-dynamic-index-borrow COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/ownership/invalid_dynamic_index_borrow.rz)
set_tests_properties(razc-reject-dynamic-index-borrow PROPERTIES WILL_FAIL TRUE)

add_test(NAME razc-check-reborrows COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/ownership/reborrows.rz)
add_test(NAME razc-mir-reborrows COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/ownership/reborrows.rz)
set_tests_properties(razc-mir-reborrows PROPERTIES PASS_REGULAR_EXPRESSION "fn @increment_reborrow.*i64&mut %input;load i64&mut;return i64&")
add_test(NAME razc-mir-reborrow-regions COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/ownership/reborrows.rz)
set_tests_properties(razc-mir-reborrow-regions PROPERTIES PASS_REGULAR_EXPRESSION "borrow.region 0 exclusive alias=input source=@caller.input;borrow.region 1 exclusive alias=child source=input.*parent=0")
add_test(NAME razc-forge-reborrows COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/ownership/reborrows.rz)
add_test(NAME razc-reject-parent-use-during-reborrow COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/ownership/invalid_reborrow_parent_use.rz)
set_tests_properties(razc-reject-parent-use-during-reborrow PROPERTIES WILL_FAIL TRUE)
add_test(NAME razc-reject-local-reference-return COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/ownership/invalid_local_reference_return.rz)
set_tests_properties(razc-reject-local-reference-return PROPERTIES WILL_FAIL TRUE)

add_test(NAME razc-check-control-flow-move-reinitialization COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/ownership/control_flow_moves.rz)
add_test(NAME razc-mir-control-flow-move-reinitialization COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/ownership/control_flow_moves.rz)
add_test(NAME razc-forge-control-flow-move-reinitialization COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/ownership/control_flow_moves.rz)
add_test(NAME razc-reject-conditional-move-use COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/ownership/invalid_conditional_move.rz)
set_tests_properties(razc-reject-conditional-move-use PROPERTIES WILL_FAIL TRUE)
add_test(NAME razc-reject-loop-move-use COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/ownership/invalid_loop_move.rz)
set_tests_properties(razc-reject-loop-move-use PROPERTIES WILL_FAIL TRUE)
add_test(NAME razc-reject-match-move-use COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/ownership/invalid_match_move.rz)
set_tests_properties(razc-reject-match-move-use PROPERTIES WILL_FAIL TRUE)

add_test(NAME razc-check-explicit-lifetimes COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/ownership/explicit_lifetimes.rz)
add_test(NAME razc-mir-explicit-lifetimes COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/ownership/explicit_lifetimes.rz)
set_tests_properties(razc-mir-explicit-lifetimes PROPERTIES PASS_REGULAR_EXPRESSION "fn @choose.*i64&'a %left.*-> i64&'a;call i64&'a choose")
add_test(NAME razc-forge-explicit-lifetimes COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/ownership/explicit_lifetimes.rz)
set_tests_properties(razc-forge-explicit-lifetimes PROPERTIES PASS_REGULAR_EXPRESSION "func @choose.*ptr.*-> ptr;func @relay.*ptr.*-> ptr")
add_test(NAME razc-reject-invalid-lifetime-relationship COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/ownership/invalid_lifetime_relationship.rz)
set_tests_properties(razc-reject-invalid-lifetime-relationship PROPERTIES WILL_FAIL TRUE)

add_test(NAME razc-check-drop-elaboration COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/ownership/drop_elaboration.rz)
add_test(NAME razc-mir-drop-elaboration COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/ownership/drop_elaboration.rz)
set_tests_properties(razc-mir-drop-elaboration PROPERTIES PASS_REGULAR_EXPRESSION "drop Resource")
add_test(NAME razc-forge-drop-elaboration COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/ownership/drop_elaboration.rz)

add_test(NAME razc-check-executable-drop COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/ownership/executable_drop.rz)
add_test(NAME razc-hir-executable-drop COMMAND razc_host --hir ${PROJECT_SOURCE_DIR}/tests/examples/ownership/executable_drop.rz)
set_tests_properties(razc-hir-executable-drop PROPERTIES PASS_REGULAR_EXPRESSION "impl Drop for Resource using @__raz_drop_Resource")
add_test(NAME razc-mir-executable-drop COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/ownership/executable_drop.rz)
set_tests_properties(razc-mir-executable-drop PROPERTIES PASS_REGULAR_EXPRESSION "call void __raz_drop_Resource;drop Resource")
add_test(NAME razc-forge-executable-drop COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/ownership/executable_drop.rz)
set_tests_properties(razc-forge-executable-drop PROPERTIES PASS_REGULAR_EXPRESSION "func @__raz_drop_Resource;call void @__raz_drop_Resource")
add_test(NAME raz-aggregate-value-semantics-audit
  COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/python/check-aggregate-value-semantics.py)
add_test(NAME razc-check-aggregate-value-semantics COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/ownership/aggregate_value_semantics.rz)
add_test(NAME razc-mir-aggregate-value-semantics COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/ownership/aggregate_value_semantics.rz)
set_tests_properties(razc-mir-aggregate-value-semantics PROPERTIES PASS_REGULAR_EXPRESSION "stack.alloc AggregateValueInner")
add_test(NAME razc-check-copy-marker COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/ownership/copy_marker.rz)
add_test(NAME razc-reject-copy-and-drop COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/ownership/invalid_copy_drop.rz)
set_tests_properties(razc-reject-copy-and-drop PROPERTIES WILL_FAIL TRUE)

add_test(NAME razc-check-tagged-enum-drop COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/ownership/tagged_enum_drop.rz)
add_test(NAME razc-mir-tagged-enum-drop COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/ownership/tagged_enum_drop.rz)
set_tests_properties(razc-mir-tagged-enum-drop PROPERTIES PASS_REGULAR_EXPRESSION "drop.enum.payload;call void __raz_drop_Resource")
add_test(NAME razc-forge-tagged-enum-drop COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/ownership/tagged_enum_drop.rz)
set_tests_properties(razc-forge-tagged-enum-drop PROPERTIES PASS_REGULAR_EXPRESSION "__raz_drop_Resource")

add_test(NAME razc-check-clone COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/ownership/clone.rz)
add_test(NAME razc-hir-clone COMMAND razc_host --hir ${PROJECT_SOURCE_DIR}/tests/examples/ownership/clone.rz)
set_tests_properties(razc-hir-clone PROPERTIES PASS_REGULAR_EXPRESSION "impl Clone for Resource using @__raz_clone_Resource;fn @__raz_clone_Resource")
add_test(NAME razc-mir-clone COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/ownership/clone.rz)
set_tests_properties(razc-mir-clone PROPERTIES PASS_REGULAR_EXPRESSION "fn @__raz_clone_Resource;const i64 99;call Resource __raz_clone_Resource;drop Resource")
add_test(NAME razc-forge-clone COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/ownership/clone.rz)
set_tests_properties(razc-forge-clone PROPERTIES PASS_REGULAR_EXPRESSION "func @__raz_clone_Resource;const i64 99;call ptr @__raz_clone_Resource")
add_test(NAME razc-reject-invalid-clone-signature COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/ownership/invalid_clone_signature.rz)
set_tests_properties(razc-reject-invalid-clone-signature PROPERTIES WILL_FAIL TRUE)

add_test(NAME razc-check-trait-bounds COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/traits/generic_bounds.rz)
add_test(NAME razc-hir-trait-bounds COMMAND razc_host --hir ${PROJECT_SOURCE_DIR}/tests/examples/traits/generic_bounds.rz)
set_tests_properties(razc-hir-trait-bounds PROPERTIES PASS_REGULAR_EXPRESSION "trait @Measurable;fn %measure;fn @preserve<T:Measurable\\+Clone>")
add_test(NAME razc-mir-trait-bounds COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/traits/generic_bounds.rz)
add_test(NAME razc-forge-trait-bounds COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/traits/generic_bounds.rz)
add_test(NAME razc-reject-unsatisfied-trait-bound COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/traits/invalid_bound.rz)
set_tests_properties(razc-reject-unsatisfied-trait-bound PROPERTIES WILL_FAIL TRUE)
add_test(NAME razc-reject-invalid-trait-impl COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/traits/invalid_impl.rz)
set_tests_properties(razc-reject-invalid-trait-impl PROPERTIES WILL_FAIL TRUE)

add_test(NAME razc-check-static-trait-dispatch COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/traits/static_dispatch.rz)
add_test(NAME razc-hir-static-trait-dispatch COMMAND razc_host --hir ${PROJECT_SOURCE_DIR}/tests/examples/traits/static_dispatch.rz)
set_tests_properties(razc-hir-static-trait-dispatch PROPERTIES PASS_REGULAR_EXPRESSION "impl Measurable for Metric method measure using @__raz_trait_Measurable_Metric_measure;fn @generic_measure<Metric>")
add_test(NAME razc-mir-static-trait-dispatch COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/traits/static_dispatch.rz)
set_tests_properties(razc-mir-static-trait-dispatch PROPERTIES PASS_REGULAR_EXPRESSION "fn @__raz_trait_Measurable_Metric_measure;call i64 __raz_trait_Measurable_Metric_scaled;fn @generic_measure<Metric>")
add_test(NAME razc-forge-static-trait-dispatch COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/traits/static_dispatch.rz)
set_tests_properties(razc-forge-static-trait-dispatch PROPERTIES PASS_REGULAR_EXPRESSION "func @__raz_trait_Measurable_Metric_measure;call i64 @__raz_trait_Measurable_Metric_scaled;call i64 @__raz_trait_Measurable_Metric_measure")
add_test(NAME razc-reject-missing-trait-method-call COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/traits/invalid_method_call.rz)
set_tests_properties(razc-reject-missing-trait-method-call PROPERTIES WILL_FAIL TRUE)

add_test(NAME razc-check-associated-items COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/traits/associated_items.rz)
add_test(NAME razc-hir-associated-items COMMAND razc_host --hir ${PROJECT_SOURCE_DIR}/tests/examples/traits/associated_items.rz)
set_tests_properties(razc-hir-associated-items PROPERTIES PASS_REGULAR_EXPRESSION "type %Item;const usize %SIZE;impl IteratorInfo for Counter type Item = i64;impl IteratorInfo for Counter const usize SIZE = 8")
add_test(NAME razc-mir-associated-items COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/traits/associated_items.rz)
add_test(NAME razc-forge-associated-items COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/traits/associated_items.rz)
add_test(NAME razc-reject-missing-associated-type COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/traits/invalid_missing_associated_type.rz)
set_tests_properties(razc-reject-missing-associated-type PROPERTIES WILL_FAIL TRUE)

add_test(NAME razc-check-fixed-array-for COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/iterators/fixed_array_for.rz)
add_test(NAME razc-mir-fixed-array-for COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/iterators/fixed_array_for.rz)
add_test(NAME razc-forge-fixed-array-for COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/iterators/fixed_array_for.rz)
add_test(NAME razc-reject-non-array-for COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/iterators/invalid_non_array_for.rz)
set_tests_properties(razc-reject-non-array-for PROPERTIES WILL_FAIL TRUE)
add_test(NAME razc-check-library-iterator-contracts COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/library/core/iter/iterator.rz)

add_test(NAME razc-check-range-for COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/iterators/range_for.rz)
add_test(NAME razc-mir-range-for COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/iterators/range_for.rz)
set_tests_properties(razc-mir-range-for PROPERTIES PASS_REGULAR_EXPRESSION "range.header;lt i64;le i64")
add_test(NAME razc-forge-range-for COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/iterators/range_for.rz)
add_test(NAME razc-reject-invalid-range-for COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/iterators/invalid_range_for.rz)
set_tests_properties(razc-reject-invalid-range-for PROPERTIES WILL_FAIL TRUE)

add_test(NAME razc-check-custom-iterator COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/iterators/custom_iterator.rz)
add_test(NAME razc-hir-custom-iterator COMMAND razc_host --hir ${PROJECT_SOURCE_DIR}/tests/examples/iterators/custom_iterator.rz)
set_tests_properties(razc-hir-custom-iterator PROPERTIES PASS_REGULAR_EXPRESSION "impl Iterator for Counter type Item = i64")
add_test(NAME razc-mir-custom-iterator COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/iterators/custom_iterator.rz)
set_tests_properties(razc-mir-custom-iterator PROPERTIES PASS_REGULAR_EXPRESSION "iter.header;call bool __raz_trait_Iterator_Counter_next;call i64 __raz_trait_Iterator_Counter_current")
add_test(NAME razc-forge-custom-iterator COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/iterators/custom_iterator.rz)
set_tests_properties(razc-forge-custom-iterator PROPERTIES PASS_REGULAR_EXPRESSION "__raz_trait_Iterator_Counter_next;__raz_trait_Iterator_Counter_current")

add_test(NAME razc-check-into-iterator COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/iterators/into_iterator.rz)
add_test(NAME razc-hir-into-iterator COMMAND razc_host --hir ${PROJECT_SOURCE_DIR}/tests/examples/iterators/into_iterator.rz)
set_tests_properties(razc-hir-into-iterator PROPERTIES PASS_REGULAR_EXPRESSION "impl IntoIterator for Numbers type IntoIter = Counter;impl IntoIterator for Numbers method into_iter")
add_test(NAME razc-mir-into-iterator COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/iterators/into_iterator.rz)
set_tests_properties(razc-mir-into-iterator PROPERTIES PASS_REGULAR_EXPRESSION "call Counter __raz_trait_IntoIterator_Numbers_into_iter;call bool __raz_trait_Iterator_Counter_next;call i64 __raz_trait_Iterator_Counter_current")
add_test(NAME razc-forge-into-iterator COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/iterators/into_iterator.rz)
set_tests_properties(razc-forge-into-iterator PROPERTIES PASS_REGULAR_EXPRESSION "__raz_trait_IntoIterator_Numbers_into_iter;__raz_trait_Iterator_Counter_next;__raz_trait_Iterator_Counter_current")
add_test(NAME razc-reject-mismatched-into-iterator-item COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/iterators/invalid_into_iterator.rz)
set_tests_properties(razc-reject-mismatched-into-iterator-item PROPERTIES WILL_FAIL TRUE)

add_test(NAME razc-check-iterator-cleanup COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/iterators/iterator_cleanup.rz)
add_test(NAME razc-mir-iterator-cleanup COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/iterators/iterator_cleanup.rz)
set_tests_properties(razc-mir-iterator-cleanup PROPERTIES PASS_REGULAR_EXPRESSION "drop Item")
add_test(NAME razc-forge-iterator-cleanup COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/iterators/iterator_cleanup.rz)

add_test(NAME razc-check-reference-iteration COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/iterators/reference_iteration.rz)
add_test(NAME razc-mir-reference-iteration COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/iterators/reference_iteration.rz)
set_tests_properties(razc-mir-reference-iteration PROPERTIES PASS_REGULAR_EXPRESSION "stack.alloc i64&mut;stack.alloc i64&;load i64&mut;load i64&")
add_test(NAME razc-forge-reference-iteration COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/iterators/reference_iteration.rz)
add_test(NAME razc-reject-shared-iteration-mutation COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/iterators/invalid_shared_iteration_mutation.rz)
set_tests_properties(razc-reject-shared-iteration-mutation PROPERTIES WILL_FAIL TRUE)

add_test(NAME razc-check-temporary-iterables COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/iterators/temporary_iterables.rz)
add_test(NAME razc-mir-temporary-iterables COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/iterators/temporary_iterables.rz)
set_tests_properties(razc-mir-temporary-iterables PROPERTIES PASS_REGULAR_EXPRESSION "call void __raz_drop_Counter;call void __raz_drop_Numbers")
add_test(NAME razc-forge-temporary-iterables COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/iterators/temporary_iterables.rz)

add_test(NAME razc-check-generic-into-iterator COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/iterators/generic_into_iterator.rz)
add_test(NAME razc-hir-generic-into-iterator COMMAND razc_host --hir ${PROJECT_SOURCE_DIR}/tests/examples/iterators/generic_into_iterator.rz)
set_tests_properties(razc-hir-generic-into-iterator PROPERTIES PASS_REGULAR_EXPRESSION "fn @count_items<Numbers>;fn @projected_identity<Numbers>\\(i64 %value\\) -> i64")
add_test(NAME razc-mir-generic-into-iterator COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/iterators/generic_into_iterator.rz)
set_tests_properties(razc-mir-generic-into-iterator PROPERTIES PASS_REGULAR_EXPRESSION "fn @count_items<Numbers>;call Counter __raz_trait_IntoIterator_Numbers_into_iter;call bool __raz_trait_Iterator_Counter_next")
add_test(NAME razc-forge-generic-into-iterator COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/iterators/generic_into_iterator.rz)
set_tests_properties(razc-forge-generic-into-iterator PROPERTIES PASS_REGULAR_EXPRESSION "func @count_items<Numbers>;__raz_trait_IntoIterator_Numbers_into_iter;func @projected_identity<Numbers>\\(%value: i64\\) -> i64")

add_test(NAME razc-check-default-trait-methods COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/traits/default_methods_supertraits.rz)
add_test(NAME razc-hir-default-trait-methods COMMAND razc_host --hir ${PROJECT_SOURCE_DIR}/tests/examples/traits/default_methods_supertraits.rz)
set_tests_properties(razc-hir-default-trait-methods PROPERTIES PASS_REGULAR_EXPRESSION "trait @Measurable : Copy;fn %doubled.*\\[default\\];impl Measurable for Metric method doubled using @__raz_trait_Measurable_Metric_doubled")
add_test(NAME razc-mir-default-trait-methods COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/traits/default_methods_supertraits.rz)
set_tests_properties(razc-mir-default-trait-methods PROPERTIES PASS_REGULAR_EXPRESSION "fn @__raz_trait_Measurable_Metric_doubled;call i64 __raz_trait_Measurable_Metric_measure;call i64 __raz_trait_Measurable_Metric_doubled")
add_test(NAME razc-forge-default-trait-methods COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/traits/default_methods_supertraits.rz)
set_tests_properties(razc-forge-default-trait-methods PROPERTIES PASS_REGULAR_EXPRESSION "func @__raz_trait_Measurable_Metric_doubled;call i64 @__raz_trait_Measurable_Metric_measure")
add_test(NAME razc-reject-missing-supertrait COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/traits/invalid_missing_supertrait.rz)
set_tests_properties(razc-reject-missing-supertrait PROPERTIES WILL_FAIL TRUE)


add_test(NAME razc-check-immediate-closure COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/closures/immediate.rz)
add_test(NAME razc-hir-immediate-closure COMMAND razc_host --hir ${PROJECT_SOURCE_DIR}/tests/examples/closures/immediate.rz)
set_tests_properties(razc-hir-immediate-closure PROPERTIES PASS_REGULAR_EXPRESSION "fn @__raz_closure_[0-9]+\\(i64 %value\\) -> i64")
add_test(NAME razc-mir-immediate-closure COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/closures/immediate.rz)
set_tests_properties(razc-mir-immediate-closure PROPERTIES PASS_REGULAR_EXPRESSION "call i64 __raz_closure_[0-9]+;fn @__raz_closure_[0-9]+")
add_test(NAME razc-forge-immediate-closure COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/closures/immediate.rz)
set_tests_properties(razc-forge-immediate-closure PROPERTIES PASS_REGULAR_EXPRESSION "call i64 @__raz_closure_[0-9]+;func @__raz_closure_[0-9]+")
add_test(NAME razc-check-immediate-closure-capture COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/closures/invalid_capture.rz)
add_test(NAME razc-reject-closure-argument-type COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/closures/invalid_argument.rz)
set_tests_properties(razc-reject-closure-argument-type PROPERTIES WILL_FAIL TRUE)
add_test(NAME razc-reject-closure-arity COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/closures/invalid_arity.rz)
set_tests_properties(razc-reject-closure-arity PROPERTIES WILL_FAIL TRUE)
add_test(NAME razc-check-callable-contracts COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/library/core/callable/callable.rz)

add_test(NAME razc-check-capturing-closures COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/closures/captures.rz)
add_test(NAME razc-hir-capturing-closures COMMAND razc_host --hir ${PROJECT_SOURCE_DIR}/tests/examples/closures/captures.rz)
set_tests_properties(razc-hir-capturing-closures PROPERTIES PASS_REGULAR_EXPRESSION "local closure<__raz_closure_;fn @__raz_closure_.*i64 %factor")
add_test(NAME razc-mir-capturing-closures COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/closures/captures.rz)
set_tests_properties(razc-mir-capturing-closures PROPERTIES PASS_REGULAR_EXPRESSION "call i64 __raz_closure_.*t[0-9]+ t[0-9]+;fn @__raz_closure_.*i64 %factor")
add_test(NAME razc-forge-capturing-closures COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/closures/captures.rz)
set_tests_properties(razc-forge-capturing-closures PROPERTIES PASS_REGULAR_EXPRESSION "call i64 @__raz_closure_;func @__raz_closure_.*%factor: i64")
add_test(NAME razc-reject-resource-capture COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/closures/invalid_resource_capture.rz)
set_tests_properties(razc-reject-resource-capture PROPERTIES WILL_FAIL TRUE)

add_test(NAME razc-check-copy-aggregate-closure-capture COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/closures/copy_aggregate_capture.rz)
add_test(NAME razc-hir-copy-aggregate-closure-capture COMMAND razc_host --hir ${PROJECT_SOURCE_DIR}/tests/examples/closures/copy_aggregate_capture.rz)
set_tests_properties(razc-hir-copy-aggregate-closure-capture PROPERTIES PASS_REGULAR_EXPRESSION "fn @__raz_closure_.*Pair %pair")
add_test(NAME razc-mir-copy-aggregate-closure-capture COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/closures/copy_aggregate_capture.rz)
set_tests_properties(razc-mir-copy-aggregate-closure-capture PROPERTIES PASS_REGULAR_EXPRESSION "stack.alloc Pair;call i64 __raz_closure_")
add_test(NAME razc-forge-copy-aggregate-closure-capture COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/closures/copy_aggregate_capture.rz)
set_tests_properties(razc-forge-copy-aggregate-closure-capture PROPERTIES PASS_REGULAR_EXPRESSION "call i64 @__raz_closure_;func @__raz_closure_.*%pair: ptr")

# Typed escaping closure environments and owned move cleanup coverage.
add_test(NAME razc-check-escaping-copy-aggregate-closure COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/closures/escaping_copy_aggregate.rz)
add_test(NAME razc-mir-escaping-copy-aggregate-closure COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/closures/escaping_copy_aggregate.rz)
set_tests_properties(razc-mir-escaping-copy-aggregate-closure PROPERTIES PASS_REGULAR_EXPRESSION "Pair 0 16 8 inline pair")
add_test(NAME razc-forge-escaping-copy-aggregate-closure COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/closures/escaping_copy_aggregate.rz)
set_tests_properties(razc-forge-escaping-copy-aggregate-closure PROPERTIES PASS_REGULAR_EXPRESSION "raz_rt_memcpy;__raz_callable_adapter_")
add_test(NAME razc-check-escaping-move-resource-closure COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/closures/escaping_move_resource.rz)
add_test(NAME razc-mir-escaping-move-resource-closure COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/closures/escaping_move_resource.rz)
set_tests_properties(razc-mir-escaping-move-resource-closure PROPERTIES PASS_REGULAR_EXPRESSION "Resource 0 8 8 inline resource __raz_async_cleanup")
add_test(NAME razc-forge-escaping-move-resource-closure COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/closures/escaping_move_resource.rz)
set_tests_properties(razc-forge-escaping-move-resource-closure PROPERTIES PASS_REGULAR_EXPRESSION "__raz_callable_drop_;call void @__raz_async_cleanup_Resource")
add_test(NAME razc-reject-noncopy-aggregate-closure-capture COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/closures/invalid_noncopy_aggregate_capture.rz)
set_tests_properties(razc-reject-noncopy-aggregate-closure-capture PROPERTIES WILL_FAIL TRUE)


# Final completion pass: borrowed closure escape safety.
add_test(NAME razc-reject-borrowed-closure-escape COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/closures/invalid_borrowed_escape.rz)
set_tests_properties(razc-reject-borrowed-closure-escape PROPERTIES WILL_FAIL TRUE)

add_test(NAME razc-check-move-closure COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/closures/move_capture.rz)
add_test(NAME razc-mir-move-closure COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/closures/move_capture.rz)
set_tests_properties(razc-mir-move-closure PROPERTIES PASS_REGULAR_EXPRESSION "fn @__raz_closure_;call i64 __raz_closure_;call void __raz_drop_Resource;drop Resource")
add_test(NAME razc-forge-move-closure COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/closures/move_capture.rz)
set_tests_properties(razc-forge-move-closure PROPERTIES PASS_REGULAR_EXPRESSION "func @__raz_closure_;call i64 @__raz_closure_;call void @__raz_drop_Resource")
add_test(NAME razc-mir-immediate-move-closure COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/closures/immediate_move_capture.rz)
set_tests_properties(razc-mir-immediate-move-closure PROPERTIES PASS_REGULAR_EXPRESSION "call i64 __raz_closure_;call void __raz_drop_Resource;drop Resource")
add_test(NAME razc-reject-use-after-move-capture COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/closures/invalid_move_capture_use.rz)
set_tests_properties(razc-reject-use-after-move-capture PROPERTIES WILL_FAIL TRUE)
add_test(NAME razc-reject-fnonce-second-call COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/closures/invalid_fnonce_twice.rz)
set_tests_properties(razc-reject-fnonce-second-call PROPERTIES WILL_FAIL TRUE)

add_test(NAME razc-check-borrow-closures COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/closures/borrow_captures.rz)
add_test(NAME razc-hir-borrow-closures COMMAND razc_host --hir ${PROJECT_SOURCE_DIR}/tests/examples/closures/borrow_captures.rz)
set_tests_properties(razc-hir-borrow-closures PROPERTIES PASS_REGULAR_EXPRESSION "fn @__raz_closure_.*i64& %value;fn @__raz_closure_.*i64&mut %value")
add_test(NAME razc-mir-borrow-closures COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/closures/borrow_captures.rz)
set_tests_properties(razc-mir-borrow-closures PROPERTIES PASS_REGULAR_EXPRESSION "call i64 __raz_closure_;load i64&;load i64&mut")
add_test(NAME razc-forge-borrow-closures COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/closures/borrow_captures.rz)
set_tests_properties(razc-forge-borrow-closures PROPERTIES PASS_REGULAR_EXPRESSION "func @__raz_closure_.*%value: ptr;call i64 @__raz_closure_")
add_test(NAME razc-reject-mut-capture-conflict COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/closures/invalid_mut_capture_conflict.rz)
set_tests_properties(razc-reject-mut-capture-conflict PROPERTIES WILL_FAIL TRUE)
add_test(NAME razc-reject-shared-capture-mutation COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/closures/invalid_shared_capture_mutation.rz)
set_tests_properties(razc-reject-shared-capture-mutation PROPERTIES WILL_FAIL TRUE)

add_test(NAME razc-check-inherent-tuple COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/aggregates/inherent_tuple.rz)
add_test(NAME razc-hir-inherent-tuple COMMAND razc_host --hir ${PROJECT_SOURCE_DIR}/tests/examples/aggregates/inherent_tuple.rz)
set_tests_properties(razc-hir-inherent-tuple PROPERTIES PASS_REGULAR_EXPRESSION "type @Pair size 16 align 8;impl Point method origin using @__raz_inherent_Point_origin;impl Point method sum using @__raz_inherent_Point_sum")
add_test(NAME razc-mir-inherent-tuple COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/aggregates/inherent_tuple.rz)
set_tests_properties(razc-mir-inherent-tuple PROPERTIES PASS_REGULAR_EXPRESSION "fn @__raz_inherent_Point_origin;fn @__raz_inherent_Point_sum;call Point __raz_inherent_Point_origin;call i64 __raz_inherent_Point_shifted")
add_test(NAME razc-forge-inherent-tuple COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/aggregates/inherent_tuple.rz)

add_test(NAME razc-check-tuples COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/aggregates/tuples.rz)
add_test(NAME razc-hir-tuples COMMAND razc_host --hir ${PROJECT_SOURCE_DIR}/tests/examples/aggregates/tuples.rz)
set_tests_properties(razc-hir-tuples PROPERTIES PASS_REGULAR_EXPRESSION "type @\\(i64,bool\\);fn @swap")
add_test(NAME razc-mir-tuples COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/aggregates/tuples.rz)
set_tests_properties(razc-mir-tuples PROPERTIES PASS_REGULAR_EXPRESSION "stack.alloc \\(i64,bool\\);ptr.offset i64;ptr.offset bool")
add_test(NAME razc-forge-tuples COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/aggregates/tuples.rz)

add_test(NAME razc-check-partial-moves COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/ownership/partial_moves.rz)
add_test(NAME razc-mir-partial-moves COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/ownership/partial_moves.rz)
set_tests_properties(razc-mir-partial-moves PROPERTIES PASS_REGULAR_EXPRESSION "drop.flag.active;drop.flag.skip;drop.flag.end")
add_test(NAME razc-forge-partial-moves COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/ownership/partial_moves.rz)
add_test(NAME razc-reject-partial-whole-use COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/ownership/invalid_partial_whole_use.rz)
set_tests_properties(razc-reject-partial-whole-use PROPERTIES WILL_FAIL TRUE)

add_test(NAME razc-check-conditional-drop COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/ownership/conditional_drop.rz)
add_test(NAME razc-mir-conditional-drop COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/ownership/conditional_drop.rz)
set_tests_properties(razc-mir-conditional-drop PROPERTIES PASS_REGULAR_EXPRESSION "drop.flag.active;call void __raz_drop_Resource;drop.flag.skip")
add_test(NAME razc-forge-conditional-drop COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/ownership/conditional_drop.rz)

add_test(NAME razc-check-resolution-cache COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/traits/resolution_cache.rz)
add_test(NAME razc-check-trait-alias-generic-impl COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/traits/trait_alias_generic_impl.rz)
add_test(NAME razc-hir-trait-alias-generic-impl COMMAND razc_host --hir ${PROJECT_SOURCE_DIR}/tests/examples/traits/trait_alias_generic_impl.rz)
set_tests_properties(razc-hir-trait-alias-generic-impl PROPERTIES PASS_REGULAR_EXPRESSION "trait @Safe = Copy\\+Clone;impl Copy for Box<T>;impl Clone for Box<T>")
add_test(NAME razc-reject-negative-trait-impl COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/traits/negative_impl.rz)
set_tests_properties(razc-reject-negative-trait-impl PROPERTIES WILL_FAIL TRUE)
add_test(NAME razc-reject-overlapping-generic-impl COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/traits/invalid_overlapping_generic_impl.rz)
set_tests_properties(razc-reject-overlapping-generic-impl PROPERTIES WILL_FAIL TRUE)

add_test(NAME razc-check-slices COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/ownership/slices.rz)
add_test(NAME razc-hir-slices COMMAND razc_host --hir ${PROJECT_SOURCE_DIR}/tests/examples/ownership/slices.rz)
set_tests_properties(razc-hir-slices PROPERTIES PASS_REGULAR_EXPRESSION "type @i64\\[\\] size 16 align 8;type @i64\\[\\]mut size 16 align 8")
add_test(NAME razc-mir-slices COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/ownership/slices.rz)
set_tests_properties(razc-mir-slices PROPERTIES PASS_REGULAR_EXPRESSION "stack.alloc i64\\[\\] 16 8;ptr.offset i64&;bounds.upper;load usize")
add_test(NAME razc-forge-slices COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/ownership/slices.rz)
set_tests_properties(razc-forge-slices PROPERTIES PASS_REGULAR_EXPRESSION "func @sum\\(%values: ptr\\) -> i64;stack.alloc ptr 16 align 8;load ptr")
add_test(NAME razc-reject-shared-slice-mutation COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/ownership/invalid_shared_slice_mutation.rz)
set_tests_properties(razc-reject-shared-slice-mutation PROPERTIES WILL_FAIL TRUE)
add_test(NAME razc-reject-mutable-slice-from-shared COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/ownership/invalid_mutable_slice_from_shared.rz)
set_tests_properties(razc-reject-mutable-slice-from-shared PROPERTIES WILL_FAIL TRUE)

# Compile-time programming, reflection, effects, traits, and const generics.
add_test(NAME razc-check-constants-comptime COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/comptime/constants_comptime.rz)
add_test(NAME razc-hir-constants-comptime COMMAND razc_host --hir ${PROJECT_SOURCE_DIR}/tests/examples/comptime/constants_comptime.rz)
set_tests_properties(razc-hir-constants-comptime PROPERTIES PASS_REGULAR_EXPRESSION "const i64 @BASE = 42;const i64 @MASK = 15")
add_test(NAME razc-mir-constants-comptime COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/comptime/constants_comptime.rz)
set_tests_properties(razc-mir-constants-comptime PROPERTIES PASS_REGULAR_EXPRESSION "const i64 42;const i64 1;add i64")
add_test(NAME razc-reject-comptime-assert COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/comptime/invalid_comptime_assert.rz)
set_tests_properties(razc-reject-comptime-assert PROPERTIES WILL_FAIL TRUE)

add_test(NAME razc-check-reflection-effects COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/reflection/reflection_effects.rz)
add_test(NAME razc-hir-reflection-effects COMMAND razc_host --hir ${PROJECT_SOURCE_DIR}/tests/examples/reflection/reflection_effects.rz)
set_tests_properties(razc-hir-reflection-effects PROPERTIES PASS_REGULAR_EXPRESSION "const usize @PAIR_SIZE = 16;const usize @PAIR_FIELDS = 2;fn @add.*\[@pure\].*\[@no_alloc\].*\[@deterministic\]")
add_test(NAME razc-reject-no-panic-contract COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/effects/invalid_no_panic.rz)
set_tests_properties(razc-reject-no-panic-contract PROPERTIES WILL_FAIL TRUE)
add_test(NAME razc-reject-pure-contract COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/effects/invalid_pure.rz)
set_tests_properties(razc-reject-pure-contract PROPERTIES WILL_FAIL TRUE)

add_test(NAME razc-check-derive COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/traits/derive.rz)
add_test(NAME razc-hir-derive COMMAND razc_host --hir ${PROJECT_SOURCE_DIR}/tests/examples/traits/derive.rz)
set_tests_properties(razc-hir-derive PROPERTIES PASS_REGULAR_EXPRESSION "impl Copy for Pair;impl Clone for Pair")
add_test(NAME razc-mir-derive COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/traits/derive.rz)
set_tests_properties(razc-mir-derive PROPERTIES PASS_REGULAR_EXPRESSION "stack.alloc Pair;return Pair")

add_test(NAME razc-check-const-functions COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/comptime/const_functions.rz)
add_test(NAME razc-hir-const-functions COMMAND razc_host --hir ${PROJECT_SOURCE_DIR}/tests/examples/comptime/const_functions.rz)
set_tests_properties(razc-hir-const-functions PROPERTIES PASS_REGULAR_EXPRESSION "const i64 @FACTORIAL_6 = 720;fn @factorial.*\[@const\]")
add_test(NAME razc-mir-const-functions COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/comptime/const_functions.rz)
set_tests_properties(razc-mir-const-functions PROPERTIES PASS_REGULAR_EXPRESSION "fn @result;const i64 720;const i64 20")


# Final completion pass: const functions share the deterministic comptime control-flow subset.
add_test(NAME razc-check-const-function-control-flow COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/comptime/const_function_control_flow.rz)
add_test(NAME razc-hir-const-function-control-flow COMMAND razc_host --hir ${PROJECT_SOURCE_DIR}/tests/examples/comptime/const_function_control_flow.rz)
set_tests_properties(razc-hir-const-function-control-flow PROPERTIES PASS_REGULAR_EXPRESSION "const i64 @VALUE = 21")

add_test(NAME razc-check-layout-attributes COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/aggregates/layout_attributes.rz)
add_test(NAME razc-hir-layout-attributes COMMAND razc_host --hir ${PROJECT_SOURCE_DIR}/tests/examples/aggregates/layout_attributes.rz)
set_tests_properties(razc-hir-layout-attributes PROPERTIES PASS_REGULAR_EXPRESSION "type @NativeHeader repr C attrs \\[repr\\(C\\)\\] size 8 align 4;type @PackedHeader repr Raz packed attrs \\[packed\\] size 5 align 1;type @CacheLineValue repr Raz requested_align 32 attrs \\[align\\(32\\)\\] size 32 align 32")
add_test(NAME razc-reject-invalid-alignment COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/aggregates/invalid_alignment.rz)
set_tests_properties(razc-reject-invalid-alignment PROPERTIES WILL_FAIL TRUE)
add_test(NAME razc-check-effect-propagation COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/effects/effect_propagation.rz)
add_test(NAME razc-hir-effect-propagation COMMAND razc_host --hir ${PROJECT_SOURCE_DIR}/tests/examples/effects/effect_propagation.rz)
add_test(NAME razc-reject-effect-propagation COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/effects/invalid_effect_propagation.rz)
set_tests_properties(razc-reject-effect-propagation PROPERTIES WILL_FAIL TRUE)

add_test(NAME razc-check-rich-reflection COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/reflection/rich_reflection.rz)
add_test(NAME razc-hir-rich-reflection COMMAND razc_host --hir ${PROJECT_SOURCE_DIR}/tests/examples/reflection/rich_reflection.rz)
set_tests_properties(razc-hir-rich-reflection PROPERTIES PASS_REGULAR_EXPRESSION "type @Header repr C;impl Copy for Pair;impl Clone for Pair")
add_test(NAME razc-reject-invalid-reflection-field COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/reflection/invalid_reflection_field.rz)
set_tests_properties(razc-reject-invalid-reflection-field PROPERTIES WILL_FAIL TRUE)

add_test(NAME razc-check-const-generics COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/generics/const_generics.rz)
add_test(NAME razc-hir-const-generics COMMAND razc_host --hir ${PROJECT_SOURCE_DIR}/tests/examples/generics/const_generics.rz)
set_tests_properties(razc-hir-const-generics PROPERTIES PASS_REGULAR_EXPRESSION "type @Buffer<T,const usize N>;type @Buffer<i64,4> \[instantiated\] size 32 align 8;fn @first<const usize N> \[template\];fn @first<4> \[instantiated\]")
add_test(NAME razc-mir-const-generics COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/generics/const_generics.rz)
set_tests_properties(razc-mir-const-generics PROPERTIES PASS_REGULAR_EXPRESSION "fn @first<4>\(i64\[4\] %values\);const i64 4;bounds.valid")
add_test(NAME razc-forge-const-generics COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/generics/const_generics.rz)
add_test(NAME razc-reject-invalid-const-generic COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/generics/invalid_const_generic.rz)
set_tests_properties(razc-reject-invalid-const-generic PROPERTIES WILL_FAIL TRUE)

add_test(NAME razc-check-const-generic-expressions COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/comptime/const_expressions.rz)
add_test(NAME razc-hir-const-generic-expressions COMMAND razc_host --hir ${PROJECT_SOURCE_DIR}/tests/examples/comptime/const_expressions.rz)
set_tests_properties(razc-hir-const-generic-expressions PROPERTIES PASS_REGULAR_EXPRESSION "type @Buffer<i64,BASE\\+2>.*size 64.*field i64\\[8\\].*fn @first<2\\+2>.*i64\\[8\\]")
add_test(NAME razc-mir-const-generic-expressions COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/comptime/const_expressions.rz)
set_tests_properties(razc-mir-const-generic-expressions PROPERTIES PASS_REGULAR_EXPRESSION "stack.alloc Buffer<i64,BASE\\+2> 64 8;fn @first<2\\+2>\\(i64\\[8\\]")
add_test(NAME razc-forge-const-generic-expressions COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/comptime/const_expressions.rz)
set_tests_properties(razc-forge-const-generic-expressions PROPERTIES PASS_REGULAR_EXPRESSION "call i64 @first<2\\+2>;func @first<2\\+2>")
add_test(NAME razc-reject-zero-const-generic-expression COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/comptime/invalid_const_expression.rz)
set_tests_properties(razc-reject-zero-const-generic-expression PROPERTIES WILL_FAIL TRUE)

add_test(NAME razc-check-const-generic-traits COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/generics/const_generic_traits.rz)
add_test(NAME razc-hir-const-generic-traits COMMAND razc_host --hir ${PROJECT_SOURCE_DIR}/tests/examples/generics/const_generic_traits.rz)
set_tests_properties(razc-hir-const-generic-traits PROPERTIES PASS_REGULAR_EXPRESSION "type @Buffer<i64,4>.*impl Copy for Buffer<i64,4>;enum @Packet<4>.*impl Clone for Packet<4>")
add_test(NAME razc-mir-const-generic-traits COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/generics/const_generic_traits.rz)
set_tests_properties(razc-mir-const-generic-traits PROPERTIES PASS_REGULAR_EXPRESSION "fn @clone_packet")
add_test(NAME razc-forge-const-generic-traits COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/generics/const_generic_traits.rz)
add_test(NAME razc-reject-const-generic-trait-kind COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/generics/invalid_const_generic_trait_kind.rz)
set_tests_properties(razc-reject-const-generic-trait-kind PROPERTIES WILL_FAIL TRUE)
add_test(NAME razc-reject-const-generic-trait-type COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/generics/invalid_const_generic_trait_type.rz)
set_tests_properties(razc-reject-const-generic-trait-type PROPERTIES WILL_FAIL TRUE)

add_test(NAME razc-check-reflection-names COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/reflection/reflection_names.rz)
add_test(NAME razc-hir-reflection-names COMMAND razc_host --hir ${PROJECT_SOURCE_DIR}/tests/examples/reflection/reflection_names.rz)
add_test(NAME razc-reject-invalid-reflection-index COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/reflection/invalid_reflection_index.rz)
set_tests_properties(razc-reject-invalid-reflection-index PROPERTIES WILL_FAIL TRUE)

add_test(NAME razc-check-aggregate-constants COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/comptime/aggregate_constants.rz)
add_test(NAME razc-hir-aggregate-constants COMMAND razc_host --hir ${PROJECT_SOURCE_DIR}/tests/examples/comptime/aggregate_constants.rz)
set_tests_properties(razc-hir-aggregate-constants PROPERTIES PASS_REGULAR_EXPRESSION "const i64 @SELECTED = 30")
add_test(NAME razc-mir-aggregate-constants COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/comptime/aggregate_constants.rz)
set_tests_properties(razc-mir-aggregate-constants PROPERTIES PASS_REGULAR_EXPRESSION "const i64 30")
add_test(NAME razc-reject-invalid-aggregate-index COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/comptime/invalid_aggregate_index.rz)
set_tests_properties(razc-reject-invalid-aggregate-index PROPERTIES WILL_FAIL TRUE)

add_test(NAME razc-check-struct-enum-constants COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/comptime/struct_enum_constants.rz)
add_test(NAME razc-hir-struct-enum-constants COMMAND razc_host --hir ${PROJECT_SOURCE_DIR}/tests/examples/comptime/struct_enum_constants.rz)
set_tests_properties(razc-hir-struct-enum-constants PROPERTIES PASS_REGULAR_EXPRESSION "const i64 @POINT_TOTAL = 42")
add_test(NAME razc-mir-struct-enum-constants COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/comptime/struct_enum_constants.rz)
set_tests_properties(razc-mir-struct-enum-constants PROPERTIES PASS_REGULAR_EXPRESSION "const i64 42")
add_test(NAME razc-reject-invalid-struct-constant COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/comptime/invalid_struct_constant.rz)
set_tests_properties(razc-reject-invalid-struct-constant PROPERTIES WILL_FAIL TRUE)

add_test(NAME razc-check-runtime-aggregate-constants COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/comptime/runtime_aggregate_constants.rz)
add_test(NAME razc-hir-runtime-aggregate-constants COMMAND razc_host --hir ${PROJECT_SOURCE_DIR}/tests/examples/comptime/runtime_aggregate_constants.rz)
set_tests_properties(razc-hir-runtime-aggregate-constants PROPERTIES PASS_REGULAR_EXPRESSION "const Point @ORIGIN;const i64\\[3\\] @VALUES;const Message @PAIR")
add_test(NAME razc-mir-runtime-aggregate-constants COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/comptime/runtime_aggregate_constants.rz)
set_tests_properties(razc-mir-runtime-aggregate-constants PROPERTIES PASS_REGULAR_EXPRESSION "fn @point_sum;alloc Point;ptr.offset i64;const i64 3;fn @array_sum;alloc i64\\[3\\];const i64 20;fn @pair_payload;alloc Message")
add_test(NAME razc-forge-runtime-aggregate-constants COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/comptime/runtime_aggregate_constants.rz)
set_tests_properties(razc-forge-runtime-aggregate-constants PROPERTIES PASS_REGULAR_EXPRESSION "func @point_sum;stack.alloc ptr;ptr.offset ptr;const i64 3;func @array_sum;const i64 20;func @pair_payload")
add_test(NAME razc-check-derived-enum-traits COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/comptime/derived_enums.rz)
add_test(NAME razc-hir-derived-enum-traits COMMAND razc_host --hir ${PROJECT_SOURCE_DIR}/tests/examples/comptime/derived_enums.rz)
set_tests_properties(razc-hir-derived-enum-traits PROPERTIES PASS_REGULAR_EXPRESSION "impl Copy for Status;impl Clone for Status")
add_test(NAME razc-reject-invalid-derived-copy-enum COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/comptime/invalid_derived_copy_enum.rz)
set_tests_properties(razc-reject-invalid-derived-copy-enum PROPERTIES WILL_FAIL TRUE)

add_test(NAME razc-check-comptime-loops COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/comptime/comptime_loops.rz)
add_test(NAME razc-hir-comptime-loops COMMAND razc_host --hir ${PROJECT_SOURCE_DIR}/tests/examples/comptime/comptime_loops.rz)
set_tests_properties(razc-hir-comptime-loops PROPERTIES PASS_REGULAR_EXPRESSION "fn @main")
add_test(NAME razc-reject-comptime-loop-limit COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/comptime/invalid_comptime_loop_limit.rz)
set_tests_properties(razc-reject-comptime-loop-limit PROPERTIES WILL_FAIL TRUE)

add_test(NAME razc-check-comptime-aggregate-mutation COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/comptime/comptime_aggregate_mutation.rz)
add_test(NAME razc-hir-comptime-aggregate-mutation COMMAND razc_host --hir ${PROJECT_SOURCE_DIR}/tests/examples/comptime/comptime_aggregate_mutation.rz)
set_tests_properties(razc-hir-comptime-aggregate-mutation PROPERTIES PASS_REGULAR_EXPRESSION "fn @main")
add_test(NAME razc-reject-comptime-aggregate-assignment-oob COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/comptime/invalid_comptime_aggregate_assignment.rz)
set_tests_properties(razc-reject-comptime-aggregate-assignment-oob PROPERTIES WILL_FAIL TRUE)

add_test(NAME razc-check-user-marker-derive COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/traits/user_marker_derive.rz)
add_test(NAME razc-hir-user-marker-derive COMMAND razc_host --hir ${PROJECT_SOURCE_DIR}/tests/examples/traits/user_marker_derive.rz)
set_tests_properties(razc-hir-user-marker-derive PROPERTIES PASS_REGULAR_EXPRESSION "impl Serializable for Packet;impl StableLayout for Packet")
add_test(NAME razc-check-user-marker-derive-enum COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/traits/user_marker_derive_enum.rz)
add_test(NAME razc-hir-user-marker-derive-enum COMMAND razc_host --hir ${PROJECT_SOURCE_DIR}/tests/examples/traits/user_marker_derive_enum.rz)
set_tests_properties(razc-hir-user-marker-derive-enum PROPERTIES PASS_REGULAR_EXPRESSION "impl WireTag for Message")
add_test(NAME razc-reject-user-derive-method-trait COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/traits/invalid_user_derive_method.rz)
set_tests_properties(razc-reject-user-derive-method-trait PROPERTIES WILL_FAIL TRUE)

# Project-driver integration.
add_test(NAME raz-project-metadata
  COMMAND raz_host metadata ${PROJECT_SOURCE_DIR}/tests/examples/projects/hello-package)
set_tests_properties(raz-project-metadata PROPERTIES PASS_REGULAR_EXPRESSION "module main.*\[entry\];module util::math")

add_test(NAME raz-project-check
  COMMAND raz_host check ${PROJECT_SOURCE_DIR}/tests/examples/projects/hello-package --target test-host --force)
set_tests_properties(raz-project-check PROPERTIES PASS_REGULAR_EXPRESSION "Checked hello-package")

add_test(NAME raz-project-build
  COMMAND raz_host build ${PROJECT_SOURCE_DIR}/tests/examples/projects/hello-package --target test-host --force)
set_tests_properties(raz-project-build PROPERTIES PASS_REGULAR_EXPRESSION "Finished `debug` profile")

add_test(NAME raz-project-incremental
  COMMAND raz_host build ${PROJECT_SOURCE_DIR}/tests/examples/projects/hello-package --target test-host)
set_tests_properties(raz-project-incremental PROPERTIES PASS_REGULAR_EXPRESSION "0 compiled, 2 fresh")

add_test(NAME raz-project-test
  COMMAND raz_host test ${PROJECT_SOURCE_DIR}/tests/examples/projects/hello-package --target test-host)
set_tests_properties(raz-project-test PROPERTIES PASS_REGULAR_EXPRESSION "Finished `debug` profile")

add_test(NAME raz-project-native-run
  COMMAND raz_host run ${PROJECT_SOURCE_DIR}/tests/examples/projects/hello-package --target test-host --force)
set_tests_properties(raz-project-native-run PROPERTIES PASS_REGULAR_EXPRESSION "Running .*/hello-package")
set_tests_properties(raz-project-check raz-project-build raz-project-incremental raz-project-test raz-project-native-run PROPERTIES RUN_SERIAL TRUE)
set_tests_properties(raz-project-incremental PROPERTIES DEPENDS raz-project-build)

# Hosted runtime and external ABI boundary.
add_test(NAME razc-check-extern-runtime COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/ffi/extern_runtime.rz)
add_test(NAME razc-forge-extern-runtime COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/ffi/extern_runtime.rz)
set_tests_properties(razc-forge-extern-runtime PROPERTIES PASS_REGULAR_EXPRESSION "extern func @raz_rt_time_monotonic_nanos;extern func @raz_rt_hardware_threads;extern func @raz_rt_process_id")
add_test(NAME razc-reject-extern-body COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/ffi/invalid_extern_body.rz)
set_tests_properties(razc-reject-extern-body PROPERTIES WILL_FAIL TRUE)

add_test(NAME raz-native-runtime
  COMMAND raz_host run ${PROJECT_SOURCE_DIR}/tests/examples/projects/runtime-interop --target test-host --force)
set_tests_properties(raz-native-runtime PROPERTIES PASS_REGULAR_EXPRESSION "Finished runtime-interop")

# Hosted standard-library capability checks.
add_executable(raz-runtime-tests ${PROJECT_SOURCE_DIR}/tests/native/runtime/runtime_tests.cpp)
target_link_libraries(raz-runtime-tests PRIVATE Raz::runtime)
add_test(NAME raz-runtime-tests COMMAND raz-runtime-tests)

add_executable(raz-async-qualification-tests ${PROJECT_SOURCE_DIR}/tests/native/runtime/async_qualification_tests.cpp)
target_link_libraries(raz-async-qualification-tests PRIVATE Raz::runtime)
add_test(NAME raz-async-qualification-tests COMMAND raz-async-qualification-tests)
set_tests_properties(raz-async-qualification-tests PROPERTIES TIMEOUT 60)

add_test(NAME razc-check-library-atomic COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/library/core/atomic/atomic.rz)
add_test(NAME razc-check-library-slice
  COMMAND ${CMAKE_COMMAND}
    -DRAZC=$<TARGET_FILE:razc_host>
    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/stdlib-slice-composed.rz
    "-DSOURCES=${PROJECT_SOURCE_DIR}/library/core/iter/iterator.rz;${PROJECT_SOURCE_DIR}/library/core/slice/slice.rz"
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_stdlib_module_with_dependencies.cmake)
add_test(NAME razc-check-library-string COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/library/alloc/string/string.rz)
add_test(NAME razc-check-library-vec COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/library/alloc/vec/vec.rz)
add_test(NAME razc-check-library-env COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/library/std/env/env.rz)
add_test(NAME razc-check-library-fs
  COMMAND ${CMAKE_COMMAND}
    -DRAZC=$<TARGET_FILE:razc_host>
    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/stdlib-fs-composed.rz
    "-DSOURCES=${PROJECT_SOURCE_DIR}/library/core/result/result.rz;${PROJECT_SOURCE_DIR}/library/std/io/error.rz;${PROJECT_SOURCE_DIR}/library/std/fs/fs.rz"
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_stdlib_module_with_dependencies.cmake)
add_test(NAME razc-check-library-sync COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/library/std/sync/mutex.rz)
add_test(NAME razc-check-library-net
  COMMAND ${CMAKE_COMMAND}
    -DRAZC=$<TARGET_FILE:razc_host>
    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/stdlib-net-composed.rz
    "-DSOURCES=${PROJECT_SOURCE_DIR}/library/core/result/result.rz;${PROJECT_SOURCE_DIR}/library/std/io/error.rz;${PROJECT_SOURCE_DIR}/library/std/net/net.rz"
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_stdlib_module_with_dependencies.cmake)
add_test(NAME razc-check-library-io COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/library/std/io/io.rz)


# Low-level systems and unsafe-pointer foundation.
add_test(NAME razc-check-unsafe-raw-pointers COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/unsafe/unsafe_raw_pointers.rz)
add_test(NAME razc-mir-unsafe-raw-pointers COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/unsafe/unsafe_raw_pointers.rz)
add_test(NAME razc-forge-unsafe-raw-pointers COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/unsafe/unsafe_raw_pointers.rz)
add_test(NAME razc-reject-raw-pointer-deref-outside-unsafe COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/unsafe/invalid_raw_pointer_deref.rz)
set_tests_properties(razc-reject-raw-pointer-deref-outside-unsafe PROPERTIES WILL_FAIL TRUE)
add_test(NAME razc-reject-unsafe-call-outside-unsafe COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/unsafe/invalid_unsafe_call.rz)
set_tests_properties(razc-reject-unsafe-call-outside-unsafe PROPERTIES WILL_FAIL TRUE)
add_test(NAME razc-check-library-ptr COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/library/core/ptr/ptr.rz)
add_test(NAME razc-check-library-hardware COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/library/core/hardware/hardware.rz)

# Async, structured tasks, SIMD, and Forge packaging coverage.
add_test(NAME razc-check-async-structured COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/async/async_structured.rz)
add_test(NAME razc-mir-async-structured COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/async/async_structured.rz)
set_tests_properties(razc-mir-async-structured PROPERTIES PASS_REGULAR_EXPRESSION "async fn @compute;async fn @pipeline;task.spawn;async.await")
add_test(NAME razc-forge-async-helpers COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/async/async_structured.rz)
set_tests_properties(razc-forge-async-helpers PROPERTIES PASS_REGULAR_EXPRESSION "raz_rt_async_frame_create;raz_rt_async_frame_set_poller;func.address ptr @compute\$async_poll;raz_rt_async_frame_future_i64;compute\$async_new;compute\$async_poll;compute\$async_drop;raz_rt_async_state_load")

add_test(NAME razc-check-generic-async COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/async/async_generic.rz)
add_test(NAME razc-mir-generic-async COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/async/async_generic.rz)
set_tests_properties(razc-mir-generic-async PROPERTIES PASS_REGULAR_EXPRESSION "async fn @use_it;identity<i64>;task.spawn;async.await")

add_test(NAME razc-reject-await-outside-async COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/async/invalid_await.rz)
set_tests_properties(razc-reject-await-outside-async PROPERTIES WILL_FAIL TRUE)
add_test(NAME razc-reject-local-reference-across-await COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/ownership/invalid_local_reference_across_await.rz)
set_tests_properties(razc-reject-local-reference-across-await PROPERTIES WILL_FAIL TRUE)
add_test(NAME razc-check-parameter-reference-across-await COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/ownership/parameter_reference_across_await.rz)
add_test(NAME razc-mir-parameter-reference-region-across-await COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/ownership/parameter_reference_across_await.rz)
set_tests_properties(razc-mir-parameter-reference-region-across-await PROPERTIES PASS_REGULAR_EXPRESSION "borrow.region 0 shared alias=input source=@caller.input.*suspension")
add_test(NAME razc-check-loop-carried-borrow-region COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/soundness/region_loop_borrow.rz)
add_test(NAME razc-mir-loop-carried-borrow-region COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/soundness/region_loop_borrow.rz)
set_tests_properties(razc-mir-loop-carried-borrow-region PROPERTIES PASS_REGULAR_EXPRESSION "borrow.region 0 exclusive alias=view source=value.*back-edge")
add_test(NAME razc-mir-async-frame-spill COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/async/async_frame_spill.rz)
set_tests_properties(razc-mir-async-frame-spill PROPERTIES PASS_REGULAR_EXPRESSION "async.slot.store;async.slot.load")
add_test(NAME razc-forge-async-frame-spill COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/async/async_frame_spill.rz)
set_tests_properties(razc-forge-async-frame-spill PROPERTIES PASS_REGULAR_EXPRESSION "raz_rt_async_slot_store;raz_rt_async_slot_load;raz_rt_async_cancel_requested;raz_rt_async_result_load;raz_rt_async_frame_cancel")
add_test(NAME razc-forge-async-await-wake COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/async/async_frame_spill.rz)
set_tests_properties(razc-forge-async-await-wake PROPERTIES PASS_REGULAR_EXPRESSION "raz_rt_async_await_poll;raz_rt_async_await_result")
add_test(NAME razc-mir-async-executable-regions COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/async/async_frame_spill.rz)
set_tests_properties(razc-mir-async-executable-regions PROPERTIES PASS_REGULAR_EXPRESSION "state.0.work;async.slot.address;call i64 compute;state.1.resume;async.result.store")
add_test(NAME razc-check-library-task COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/library/std/thread/task.rz)
add_test(NAME razc-check-library-simd COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/library/core/simd/simd.rz)

# Concurrency and portable SIMD coverage.
add_test(NAME razc-check-library-condition COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/library/std/sync/condition.rz)
add_test(NAME razc-check-library-rwlock COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/library/std/sync/rwlock.rz)
add_test(NAME razc-check-library-cancellation COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/library/std/thread/cancellation.rz)

add_test(NAME razc-check-library-latch COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/library/std/thread/latch.rz)

# Message-passing and coordination coverage.
add_test(NAME razc-check-library-semaphore COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/library/std/sync/semaphore.rz)
add_test(NAME razc-check-library-barrier COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/library/std/sync/barrier.rz)
add_test(NAME razc-check-library-once COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/library/std/sync/once.rz)
add_test(NAME razc-check-library-channel COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/library/std/thread/channel.rz)

# Future state and continuation coverage.
add_test(NAME razc-check-library-future COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/library/std/thread/future.rz)

# Bounded executor and scalable future scheduling.
add_test(NAME razc-check-library-executor COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/library/std/thread/executor.rz)

# Wake-driven timers and future orchestration.
add_test(NAME razc-check-library-timer COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/library/std/thread/timer.rz)

# Wake-driven socket readiness integration.
add_test(NAME razc-check-library-io-reactor COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/library/std/net/reactor.rz)

# Async filesystem integration.
add_test(NAME razc-check-library-async-fs COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/library/std/fs/async_fs.rz)

# Host ABI introspection and conformance.
add_test(NAME razc-check-library-abi COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/library/core/abi/abi.rz)

# Executor-backed asynchronous socket operations.
add_test(NAME razc-check-library-async-socket COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/library/std/net/async_socket/async_socket.rz)

# Production toolchain and backend qualification.
foreach(mode IN ITEMS fmt lint doc lock verify bench profile coverage fuzz sbom audit testreport package publish install graph doctor cache)
  add_test(NAME raz-project-${mode}
    COMMAND ${CMAKE_COMMAND}
      -DRAZ=$<TARGET_FILE:raz_host>
      -DSOURCE=${PROJECT_SOURCE_DIR}/tests/examples/projects/tooling-package
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/tooling-${mode}
      -DMODE=${mode}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tooling_command_test.cmake)
endforeach()

add_test(NAME raz-project-lsp
  COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/python/check-lsp-semantic-index.py
    --raz $<TARGET_FILE:raz_host>)
set_tests_properties(raz-project-lsp PROPERTIES RUN_SERIAL TRUE TIMEOUT 60)
add_test(NAME razc-mir-async-cfg-regions COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/async/async_cfg_regions.rz)
set_tests_properties(razc-mir-async-cfg-regions PROPERTIES PASS_REGULAR_EXPRESSION "region state=0;region state=1;back-edge")

add_test(NAME razc-check-function-pointers COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/callables/function_pointers.rz)
add_test(NAME razc-mir-function-pointers COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/callables/function_pointers.rz)
set_tests_properties(razc-mir-function-pointers PROPERTIES PASS_REGULAR_EXPRESSION "func.address fn\\(i64,i64\\)->i64 add;call.indirect i64")
add_test(NAME razc-forge-function-pointers COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/callables/function_pointers.rz)
set_tests_properties(razc-forge-function-pointers PROPERTIES PASS_REGULAR_EXPRESSION "func.address ptr @add;call.indirect i64.*as @add")

# noncapturing closure coercion to first-class function pointers.
add_test(NAME razc-check-closure-function-coercion COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/function_pointers/closure_coercion.rz)
add_test(NAME razc-mir-closure-function-coercion COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/function_pointers/closure_coercion.rz)
set_tests_properties(razc-mir-closure-function-coercion PROPERTIES PASS_REGULAR_EXPRESSION "func.address fn\\(i64\\)->i64 __raz_closure_;call.indirect i64")
add_test(NAME razc-forge-closure-function-coercion COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/function_pointers/closure_coercion.rz)
set_tests_properties(razc-forge-closure-function-coercion PROPERTIES PASS_REGULAR_EXPRESSION "func.address ptr @__raz_closure_;call.indirect i64")
add_test(NAME razc-reject-capturing-closure-function-coercion COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/function_pointers/invalid_capturing_closure_coercion.rz)
set_tests_properties(razc-reject-capturing-closure-function-coercion PROPERTIES WILL_FAIL TRUE)

# explicit callable-trait identity for stored closures.
add_test(NAME razc-check-closure-callable-kinds COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/closures/callable_kinds.rz)
add_test(NAME razc-hir-closure-callable-kinds COMMAND razc_host --hir ${PROJECT_SOURCE_DIR}/tests/examples/closures/callable_kinds.rz)
set_tests_properties(razc-hir-closure-callable-kinds PROPERTIES PASS_REGULAR_EXPRESSION "local Fn<__raz_closure_;local FnMut<__raz_closure_;local FnOnce<__raz_closure_")
add_test(NAME razc-hir-fnonce-single-consumption COMMAND razc_host --hir ${PROJECT_SOURCE_DIR}/tests/examples/closures/move_capture.rz)
set_tests_properties(razc-hir-fnonce-single-consumption PROPERTIES PASS_REGULAR_EXPRESSION "local FnOnce<__raz_closure_")

# public callable signature types and capability compatibility.
add_test(NAME razc-check-callable-signature-bounds COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/closures/callable_signature_bounds.rz)
add_test(NAME razc-reject-callable-kind-strength COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/closures/invalid_callable_kind_strength.rz)
set_tests_properties(razc-reject-callable-kind-strength PROPERTIES WILL_FAIL TRUE)

# owned scalar capture environments for escaping callables.
add_test(NAME razc-check-owned-escaping-closure COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/closures/escaping_owned.rz)
add_test(NAME razc-mir-owned-escaping-closure COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/closures/escaping_owned.rz)
set_tests_properties(razc-mir-owned-escaping-closure PROPERTIES PASS_REGULAR_EXPRESSION "callable.create FnOnce\\(i64\\)->i64;callable.invoke i64;callable.destroy")
add_test(NAME razc-forge-owned-escaping-closure COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/closures/escaping_owned.rz)
set_tests_properties(razc-forge-owned-escaping-closure PROPERTIES PASS_REGULAR_EXPRESSION "__raz_callable_adapter_;raz_rt_callable_create_erased;raz_rt_callable_invoke_erased;raz_rt_callable_destroy_erased")

# reference-counted callable environments and runtime callable-kind enforcement.
add_test(NAME razc-check-shared-callable-clone COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/closures/shared_callable_clone.rz)
add_test(NAME razc-mir-shared-callable-clone COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/closures/shared_callable_clone.rz)
set_tests_properties(razc-mir-shared-callable-clone PROPERTIES PASS_REGULAR_EXPRESSION "callable.clone Fn<")
add_test(NAME razc-forge-shared-callable-clone COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/closures/shared_callable_clone.rz)
set_tests_properties(razc-forge-shared-callable-clone PROPERTIES PASS_REGULAR_EXPRESSION "raz_rt_callable_clone_erased")
add_test(NAME razc-reject-fnonce-clone COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/closures/invalid_fnonce_clone.rz)
set_tests_properties(razc-reject-fnonce-clone PROPERTIES WILL_FAIL TRUE)

# expanded owned callable machine-word ABI.
add_test(NAME razc-check-wide-owned-callable COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/closures/wide_owned_callable.rz)
add_test(NAME razc-mir-wide-owned-callable COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/closures/wide_owned_callable.rz)
set_tests_properties(razc-mir-wide-owned-callable PROPERTIES PASS_REGULAR_EXPRESSION "callable.create FnOnce.*i64 0 8 8 value t[0-9]+.*i64 56 8 8 value t[0-9]+")
add_test(NAME razc-forge-wide-owned-callable COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/closures/wide_owned_callable.rz)
set_tests_properties(razc-forge-wide-owned-callable PROPERTIES PASS_REGULAR_EXPRESSION "__raz_callable_adapter_;raz_rt_callable_create_erased;raz_rt_callable_invoke_erased")

add_test(NAME razc-check-library-callable-weak COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/library/core/callable/weak.rz)

add_test(NAME razc-check-dynamic-trait-type COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/traits/dynamic_object_safe.rz)
add_test(NAME razc-hir-dynamic-trait-vtable COMMAND razc_host --hir ${PROJECT_SOURCE_DIR}/tests/examples/traits/dynamic_object_safe.rz)
set_tests_properties(razc-hir-dynamic-trait-vtable PROPERTIES PASS_REGULAR_EXPRESSION "trait @Render \\[object-safe\\];fn %render.*\\[vtable=0\\];fn %scaled.*\\[vtable=1\\];dyn Render %value")
add_test(NAME razc-reject-object-unsafe-dynamic-trait COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/traits/invalid_dynamic_object_unsafe.rz)
set_tests_properties(razc-reject-object-unsafe-dynamic-trait PROPERTIES WILL_FAIL TRUE)

# Native dynamic trait objects and vtable dispatch coverage.
add_test(NAME razc-check-dynamic-trait-dispatch COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/traits/dynamic_dispatch.rz)
add_test(NAME razc-mir-dynamic-trait-dispatch COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/traits/dynamic_dispatch.rz)
set_tests_properties(razc-mir-dynamic-trait-dispatch PROPERTIES PASS_REGULAR_EXPRESSION "trait.object.create;trait.object.invoke;trait.object.destroy")
add_test(NAME razc-forge-dynamic-trait-dispatch COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/traits/dynamic_dispatch.rz)
set_tests_properties(razc-forge-dynamic-trait-dispatch PROPERTIES PASS_REGULAR_EXPRESSION "__raz_trait_adapter_;raz_rt_trait_object_create_erased;raz_rt_trait_object_invoke_erased;raz_rt_trait_object_destroy_erased")
add_test(NAME raz-project-dynamic-trait-dispatch
  COMMAND raz_host run ${PROJECT_SOURCE_DIR}/tests/examples/projects/dynamic-trait-dispatch --target test-host --force)
set_tests_properties(raz-project-dynamic-trait-dispatch PROPERTIES PASS_REGULAR_EXPRESSION "Finished dynamic-trait-dispatch")

# Shared dynamic trait-object handles.
add_test(NAME razc-check-dynamic-trait-clone COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/traits/dynamic_trait_clone.rz)
add_test(NAME razc-mir-dynamic-trait-clone COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/traits/dynamic_trait_clone.rz)
set_tests_properties(razc-mir-dynamic-trait-clone PROPERTIES PASS_REGULAR_EXPRESSION "trait.object.clone;trait.object.destroy")
add_test(NAME razc-forge-dynamic-trait-clone COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/traits/dynamic_trait_clone.rz)
set_tests_properties(razc-forge-dynamic-trait-clone PROPERTIES PASS_REGULAR_EXPRESSION "raz_rt_trait_object_clone_erased;raz_rt_trait_object_destroy_erased")
add_test(NAME razc-check-library-trait-object-weak COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/library/core/trait_object/weak.rz)

# deterministic concrete-type identity and checked dyn Trait downcasting.
add_test(NAME razc-check-library-trait-object-type-identity COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/library/core/trait_object/type_identity.rz)

# deterministic trait identity for dyn Trait handles.
add_test(NAME razc-check-library-trait-object-trait-identity COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/library/core/trait_object/trait_identity.rz)

add_test(NAME razc-mir-callable-abi-descriptor COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/closures/escaping_owned.rz)
set_tests_properties(razc-mir-callable-abi-descriptor PROPERTIES PASS_REGULAR_EXPRESSION "abi.dispatch callable.*args=8/8 result=8/8 FnOnce\\(i64\\)->i64;field i64 @0 size=8 align=8")
add_test(NAME razc-mir-trait-abi-descriptor COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/traits/dynamic_dispatch.rz)
set_tests_properties(razc-mir-trait-abi-descriptor PROPERTIES PASS_REGULAR_EXPRESSION "abi.dispatch trait.method.*Render::render\\(\\)->i64;abi.dispatch trait.method.*args=8/8.*Render::scaled\\(i64\\)->i64")

add_test(NAME razc-check-enum-payload-moves COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/ownership/enum_payload_moves.rz)
add_test(NAME razc-mir-enum-payload-moves COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/ownership/enum_payload_moves.rz)
set_tests_properties(razc-mir-enum-payload-moves PROPERTIES PASS_REGULAR_EXPRESSION "drop.flag.active;call void __raz_drop_Resource;drop.enum.payload")
add_test(NAME razc-forge-enum-payload-moves COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/ownership/enum_payload_moves.rz)
set_tests_properties(razc-forge-enum-payload-moves PROPERTIES PASS_REGULAR_EXPRESSION "__raz_drop_Resource")
add_test(NAME razc-reject-enum-use-after-payload-move COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/ownership/invalid_enum_payload_use.rz)
set_tests_properties(razc-reject-enum-use-after-payload-move PROPERTIES WILL_FAIL TRUE)

add_test(NAME razc-check-async-owned-callable-spill COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/ownership/async_owned_callable_spill.rz)
add_test(NAME razc-mir-async-owned-callable-spill COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/ownership/async_owned_callable_spill.rz)
set_tests_properties(razc-mir-async-owned-callable-spill PROPERTIES PASS_REGULAR_EXPRESSION "owned cleanup=@raz_rt_callable_destroy_erased;async.slot.store.owned;async.slot.disarm")
add_test(NAME razc-forge-async-owned-callable-spill COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/ownership/async_owned_callable_spill.rz)
set_tests_properties(razc-forge-async-owned-callable-spill PROPERTIES PASS_REGULAR_EXPRESSION "call void @raz_rt_async_slot_store_owned;call void @raz_rt_async_slot_disarm;func.address ptr @raz_rt_callable_destroy_erased")


# compiler-generated recursive cleanup for owned aggregate async spills.
add_test(NAME razc-check-async-owned-aggregate-spill COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/ownership/async_owned_aggregate_spill.rz)
add_test(NAME razc-mir-async-owned-aggregate-spill COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/ownership/async_owned_aggregate_spill.rz)
set_tests_properties(razc-mir-async-owned-aggregate-spill PROPERTIES PASS_REGULAR_EXPRESSION "owned.bytes size=16 align=8 projections=2 cleanup=@__raz_async_cleanup\$Bundle;async.slot.allocate.projected.bytes;async.slot.transfer.projected;fn @__raz_async_cleanup\$Bundle")
add_test(NAME razc-forge-async-owned-aggregate-spill COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/ownership/async_owned_aggregate_spill.rz)
set_tests_properties(razc-forge-async-owned-aggregate-spill PROPERTIES PASS_REGULAR_EXPRESSION "raz_rt_async_slot_allocate_projected_bytes;raz_rt_async_slot_transfer_projected;func.address ptr @__raz_async_cleanup\$Bundle;func @__raz_async_cleanup\$Bundle;call void @__raz_drop_Resource")

# synchronize lexical drop flags into async projection masks and guard cleanup per bit.
add_test(NAME razc-check-async-partial-move-spill COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/ownership/async_partial_move_spill.rz)
add_test(NAME razc-mir-async-partial-move-spill COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/ownership/async_partial_move_spill.rz)
set_tests_properties(razc-mir-async-partial-move-spill PROPERTIES PASS_REGULAR_EXPRESSION "projection.changed.*async.slot.projection.batch.set i64 frame 0 2 0 1;async.slot.transfer.projected;bit.and u64 projection_mask")
add_test(NAME razc-forge-async-partial-move-spill COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/ownership/async_partial_move_spill.rz)
set_tests_properties(razc-forge-async-partial-move-spill PROPERTIES PASS_REGULAR_EXPRESSION "call void @raz_rt_async_slot_projection_set;and i64 %projection_mask;func @__raz_async_cleanup\\$Bundle")

# projected async slots carry an explicit bounded projection count.
add_test(NAME razc-mir-async-projection-count COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/ownership/async_partial_move_spill.rz)
set_tests_properties(razc-mir-async-projection-count PROPERTIES PASS_REGULAR_EXPRESSION "async.slot.allocate.projected.bytes Bundle;__raz_async_cleanup\$Bundle 3 2")
add_test(NAME razc-forge-async-projection-count COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/ownership/async_partial_move_spill.rz)
set_tests_properties(razc-forge-async-projection-count PROPERTIES PASS_REGULAR_EXPRESSION "%projection_count: i64;raz_rt_async_slot_allocate_projected_bytes")

# compiler suspension lowering publishes multiple projection transitions atomically.
# batch operations retain a changed-projection delta result and suppress redundant runtime updates.
add_test(NAME razc-mir-async-projection-batch-lowering COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/ownership/async_partial_move_spill.rz)
set_tests_properties(razc-mir-async-projection-batch-lowering PROPERTIES PASS_REGULAR_EXPRESSION "projection.changed.*async.slot.projection.batch.set i64 frame 0 2 0 1;async.slot.projection.set bool frame 4 0")
add_test(NAME razc-forge-async-projection-batch-lowering COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/ownership/async_partial_move_spill.rz)
set_tests_properties(razc-forge-async-projection-batch-lowering PROPERTIES PASS_REGULAR_EXPRESSION "call i64 @raz_rt_async_slot_projection_batch_set;stack.alloc 16 8;store i64 0;store i64 1")

# deterministic typed reflection over inherent methods.
# deterministic trait and associated-item reflection.
add_test(NAME razc-check-comptime-method-reflection COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/comptime/method_reflection.rz)
add_test(NAME razc-reject-comptime-method-reflection-oob COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/comptime/invalid_method_reflection.rz)
set_tests_properties(razc-reject-comptime-method-reflection-oob PROPERTIES WILL_FAIL TRUE)
add_test(NAME razc-check-comptime-trait-reflection COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/comptime/trait_reflection.rz)
add_test(NAME razc-reject-comptime-trait-reflection-oob COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/comptime/invalid_trait_reflection.rz)
add_test(NAME razc-check-comptime-implementation-reflection COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/comptime/implementation_reflection.rz)
add_test(NAME razc-reject-comptime-implementation-reflection-missing COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/comptime/invalid_implementation_reflection.rz)
set_tests_properties(razc-reject-comptime-trait-reflection-oob PROPERTIES WILL_FAIL TRUE)

set_tests_properties(razc-reject-comptime-implementation-reflection-missing PROPERTIES WILL_FAIL TRUE)

# generic trait implementation associated bindings.
add_test(NAME razc-check-comptime-generic-implementation-reflection COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/comptime/generic_implementation_reflection.rz)
add_test(NAME razc-reject-generic-implementation-missing-binding COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/comptime/invalid_generic_implementation_binding.rz)
set_tests_properties(razc-reject-generic-implementation-missing-binding PROPERTIES WILL_FAIL TRUE)

# Production generic trait method materialization
add_test(NAME razc-check-generic-trait-methods COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/traits/generic_trait_methods.rz)
add_test(NAME razc-hir-generic-trait-methods COMMAND razc_host --hir ${PROJECT_SOURCE_DIR}/tests/examples/traits/generic_trait_methods.rz)
set_tests_properties(razc-hir-generic-trait-methods PROPERTIES PASS_REGULAR_EXPRESSION "impl Provider for Box<i64> method get using @__raz_trait_Provider_Box<i64>_get;impl Provider for Box<i64> method doubled using @__raz_trait_Provider_Box<i64>_doubled")
add_test(NAME razc-mir-generic-trait-methods COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/traits/generic_trait_methods.rz)
set_tests_properties(razc-mir-generic-trait-methods PROPERTIES PASS_REGULAR_EXPRESSION "fn @__raz_trait_Provider_Box<i64>_get;fn @__raz_trait_Provider_Box<i64>_doubled;call i64 __raz_trait_Provider_Box<i64>_get")
add_test(NAME razc-forge-generic-trait-methods COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/traits/generic_trait_methods.rz)
set_tests_properties(razc-forge-generic-trait-methods PROPERTIES PASS_REGULAR_EXPRESSION "func @__raz_trait_Provider_Box<i64>_get;func @__raz_trait_Provider_Box<i64>_doubled")
add_test(NAME razc-reject-generic-trait-method-signature COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/traits/invalid_generic_trait_method_signature.rz)
set_tests_properties(razc-reject-generic-trait-method-signature PROPERTIES WILL_FAIL TRUE)
add_test(NAME razc-reject-generic-trait-missing-method COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/traits/invalid_generic_trait_missing_method.rz)
set_tests_properties(razc-reject-generic-trait-missing-method PROPERTIES WILL_FAIL TRUE)
add_test(NAME razc-reject-generic-trait-extra-method COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/traits/invalid_generic_trait_extra_method.rz)
set_tests_properties(razc-reject-generic-trait-extra-method PROPERTIES WILL_FAIL TRUE)

# const-generic associated constants in generic trait methods.
add_test(NAME razc-check-generic-trait-associated-constants COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/traits/generic_trait_associated_constants.rz)
add_test(NAME razc-hir-generic-trait-associated-constants COMMAND razc_host --hir ${PROJECT_SOURCE_DIR}/tests/examples/traits/generic_trait_associated_constants.rz)
set_tests_properties(razc-hir-generic-trait-associated-constants PROPERTIES PASS_REGULAR_EXPRESSION "associated const Capacity for Buffer<i64,4>::WIDTH: usize = 8;impl Capacity for Buffer<i64,4> method width")
add_test(NAME razc-mir-generic-trait-associated-constants COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/traits/generic_trait_associated_constants.rz)
set_tests_properties(razc-mir-generic-trait-associated-constants PROPERTIES PASS_REGULAR_EXPRESSION "fn @__raz_trait_Capacity_Buffer<i64,4>_width;const i64 8")
add_test(NAME razc-forge-generic-trait-associated-constants COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/traits/generic_trait_associated_constants.rz)
set_tests_properties(razc-forge-generic-trait-associated-constants PROPERTIES PASS_REGULAR_EXPRESSION "func @__raz_trait_Capacity_Buffer<i64,4>_width")
add_test(NAME razc-reject-generic-trait-associated-constant-expression COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/traits/invalid_generic_trait_associated_constant_expression.rz)
set_tests_properties(razc-reject-generic-trait-associated-constant-expression PROPERTIES WILL_FAIL TRUE)

# unification-based generic trait coherence and partial patterns.
add_test(NAME razc-check-generic-trait-partial-patterns COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/traits/generic_trait_partial_patterns.rz)
add_test(NAME razc-hir-generic-trait-partial-patterns COMMAND razc_host --hir ${PROJECT_SOURCE_DIR}/tests/examples/traits/generic_trait_partial_patterns.rz)
set_tests_properties(razc-hir-generic-trait-partial-patterns PROPERTIES PASS_REGULAR_EXPRESSION "impl Select for Pair<i64,i64> method select;impl Select for Wrapper<Pair<i64,i64>> method select;impl Select for Buffer<i64,4> method select")
add_test(NAME razc-mir-generic-trait-partial-patterns COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/traits/generic_trait_partial_patterns.rz)
set_tests_properties(razc-mir-generic-trait-partial-patterns PROPERTIES PASS_REGULAR_EXPRESSION "__raz_trait_Select_Pair<i64,i64>_select;__raz_trait_Select_Wrapper<Pair<i64,i64>>_select;__raz_trait_Select_Buffer<i64,4>_select")
add_test(NAME razc-forge-generic-trait-partial-patterns COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/traits/generic_trait_partial_patterns.rz)
set_tests_properties(razc-forge-generic-trait-partial-patterns PROPERTIES PASS_REGULAR_EXPRESSION "func @__raz_trait_Select_Pair<i64,i64>_select;func @__raz_trait_Select_Wrapper<Pair<i64,i64>>_select;func @__raz_trait_Select_Buffer<i64,4>_select")
add_test(NAME razc-reject-generic-trait-partial-overlap COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/traits/invalid_generic_trait_partial_overlap.rz)
set_tests_properties(razc-reject-generic-trait-partial-overlap PROPERTIES WILL_FAIL TRUE)
add_test(NAME razc-reject-generic-trait-repeated-parameter-mismatch COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/traits/invalid_generic_trait_repeated_parameter.rz)
set_tests_properties(razc-reject-generic-trait-repeated-parameter-mismatch PROPERTIES WILL_FAIL TRUE)
add_test(NAME razc-reject-generic-trait-unconstrained-parameter COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/traits/invalid_generic_trait_unconstrained_parameter.rz)
set_tests_properties(razc-reject-generic-trait-unconstrained-parameter PROPERTIES WILL_FAIL TRUE)

# deterministic package trait/implementation interfaces and dependency invalidation.
add_test(NAME raz-project-interface-metadata
  COMMAND ${CMAKE_COMMAND}
    -DRAZ_EXE=$<TARGET_FILE:raz_host>
    -DFIXTURE_ROOT=${PROJECT_SOURCE_DIR}/tests/examples/projects
    -DWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/interface-metadata-work
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/project_interface_metadata.cmake)

add_executable(raz-backend-abi-qualification-tests
    ${PROJECT_SOURCE_DIR}/tests/native/runtime/backend_abi_qualification_tests.cpp)
target_link_libraries(raz-backend-abi-qualification-tests PRIVATE Raz::runtime forge)
add_test(NAME raz-backend-abi-qualification-tests COMMAND raz-backend-abi-qualification-tests)

add_executable(raz-runtime-process-arguments-tests
    ${PROJECT_SOURCE_DIR}/tests/native/runtime/process_arguments_tests.cpp)
target_link_libraries(raz-runtime-process-arguments-tests PRIVATE Raz::runtime)
add_test(NAME raz-runtime-process-arguments-tests
    COMMAND raz-runtime-process-arguments-tests "manifest path with spaces.txt" "explicit-output.fir")

# Native artifact and shipped standard-library qualification gates.
add_test(NAME raz-native-artifact-builds
  COMMAND ${CMAKE_COMMAND}
    -DRAZ_EXE=$<TARGET_FILE:raz_host>
    -DFIXTURE_ROOT=${PROJECT_SOURCE_DIR}/tests/examples/projects
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/native_artifact_builds.cmake)
set_tests_properties(raz-native-artifact-builds PROPERTIES RUN_SERIAL TRUE TIMEOUT 180)
add_test(NAME raz-stdlib-library-surface
  COMMAND ${CMAKE_COMMAND}
    -DRAZC_EXE=$<TARGET_FILE:razc_host>
    -DFORGE_CODEGEN=$<TARGET_FILE:raz-compiler-forge-codegen>
    -DRAZ_RUNTIME_LIB=$<TARGET_FILE:raz_runtime>
    -DSOURCE_ROOT=${PROJECT_SOURCE_DIR}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/stdlib_library_surface.cmake)
set_tests_properties(raz-stdlib-library-surface PROPERTIES TIMEOUT 180)

# Deterministic production toolchain qualification.
add_test(NAME raz-toolchain-qualification
  COMMAND ${CMAKE_COMMAND}
    -DRAZ_EXE=$<TARGET_FILE:raz_host>
    -DSOURCE=${PROJECT_SOURCE_DIR}/tests/examples/projects/tooling-package
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/toolchain-qualification-work
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/toolchain_qualification.cmake)
set_tests_properties(raz-toolchain-qualification PROPERTIES RUN_SERIAL TRUE TIMEOUT 240)

# Compiler object-emission helper. Raz embeds Forge as a library with
# command-line tools disabled, so the bootstrap gate builds only the codegen
# driver it needs.
add_executable(raz-compiler-forge-codegen
  ${PROJECT_SOURCE_DIR}/src/forge/tools/forge-codegen/main.cpp)
target_link_libraries(raz-compiler-forge-codegen PRIVATE forge)

# Production compiler frontend qualification.
add_test(NAME raz-compiler-frontend-qualification
  COMMAND ${CMAKE_COMMAND}
    -DRAZ_EXE=$<TARGET_FILE:raz_host>
    -DRAZC_EXE=$<TARGET_FILE:razc_host>
    -DFORGE_CODEGEN=$<TARGET_FILE:raz-compiler-forge-codegen>
    -DRAZ_RUNTIME_LIB=$<TARGET_FILE:raz_runtime>
    -DSOURCE_ROOT=${PROJECT_SOURCE_DIR}
    -DWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/compiler-frontend-work
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/compiler_frontend_qualification.cmake)
set_tests_properties(raz-compiler-frontend-qualification PROPERTIES RUN_SERIAL TRUE TIMEOUT 240)
# Host-compiler gate: the native host compiler constructs the complete Raz-written compiler.
add_test(NAME raz-compiler-host-build
  COMMAND ${CMAKE_COMMAND}
    -DRAZ_EXE=$<TARGET_FILE:raz_host>
    -DRAZC_EXE=$<TARGET_FILE:razc_host>
    -DSOURCE_ROOT=${PROJECT_SOURCE_DIR}
    -DWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/compiler-host-build-work
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/compiler_host_build.cmake)
set_tests_properties(raz-compiler-host-build PROPERTIES RUN_SERIAL TRUE TIMEOUT 240)


# Compiler IR gate: the generated compiler must read the complete compiler source
# and emit a substantial Forge module with the required reference ABI.
add_test(NAME raz-compiler-ir-qualification
  COMMAND ${CMAKE_COMMAND}
    -DRAZ_EXE=$<TARGET_FILE:raz_host>
    -DSOURCE_ROOT=${PROJECT_SOURCE_DIR}
    -DWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/compiler-ir-work
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/compiler_ir_qualification.cmake)
set_tests_properties(raz-compiler-ir-qualification PROPERTIES RUN_SERIAL TRUE TIMEOUT 240)

# Production compiler gate: a generated compiler emits a native compiler,
# compiles the qualified smoke package, and executes it as native code.
add_test(NAME raz-compiler-production-qualification
  COMMAND ${CMAKE_COMMAND}
    -DRAZ_EXE=$<TARGET_FILE:raz_host>
    -DFORGE_CODEGEN=$<TARGET_FILE:raz-compiler-forge-codegen>
    -DRAZ_RUNTIME_LIB=$<TARGET_FILE:raz_runtime>
    -DRAZ_RUNTIME_LINK_MANIFEST=${CMAKE_BINARY_DIR}/raz-runtime-link-deps.txt
    -DCXX_COMPILER=${CMAKE_CXX_COMPILER}
    -DRAZ_FORGE_BRIDGE_LIB=$<TARGET_FILE:raz_forge_bridge>
    -DFORGE_LIB=$<TARGET_FILE:forge>
    -DSOURCE_ROOT=${PROJECT_SOURCE_DIR}
    -DWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/compiler-production-work
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/compiler_production_qualification.cmake)
set_tests_properties(raz-compiler-production-qualification PROPERTIES RUN_SERIAL TRUE TIMEOUT 360)

# Regression gate: the Raz-written compiler must preserve assignments to
# function parameters.  Dropping these stores caused the recursive project
# loader's whitespace loops to run forever in a self-generated compiler.
add_test(NAME raz-compiler-mutable-parameters
  COMMAND ${CMAKE_COMMAND}
    -DRAZ_EXE=$<TARGET_FILE:raz_host>
    -DFORGE_CODEGEN=$<TARGET_FILE:raz-compiler-forge-codegen>
    -DRAZ_RUNTIME_LIB=$<TARGET_FILE:raz_runtime>
    -DRAZ_RUNTIME_LINK_MANIFEST=${CMAKE_BINARY_DIR}/raz-runtime-link-deps.txt
    -DCXX_COMPILER=${CMAKE_CXX_COMPILER}
    -DSOURCE_ROOT=${PROJECT_SOURCE_DIR}
    -DWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/compiler-mutable-parameters-work
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/compiler_mutable_parameters.cmake)
set_tests_properties(raz-compiler-mutable-parameters PROPERTIES RUN_SERIAL TRUE TIMEOUT 120)

# Compiler reproducibility gate: recursively generated compilers use the explicit
# manifest/output CLI and must converge to identical Forge modules.
add_test(NAME raz-compiler-reproducibility
  COMMAND ${CMAKE_COMMAND}
    -DRAZ_EXE=$<TARGET_FILE:raz_host>
    -DRAZ_RUNTIME_LIB=$<TARGET_FILE:raz_runtime>
    -DRAZ_RUNTIME_LINK_MANIFEST=${CMAKE_BINARY_DIR}/raz-runtime-link-deps.txt
    -DCXX_COMPILER=${CMAKE_CXX_COMPILER}
    -DRAZ_FORGE_BRIDGE_LIB=$<TARGET_FILE:raz_forge_bridge>
    -DFORGE_LIB=$<TARGET_FILE:forge>
    -DSOURCE_ROOT=${PROJECT_SOURCE_DIR}
    -DWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/compiler-reproducibility-work
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/compiler_reproducibility.cmake)
set_tests_properties(raz-compiler-reproducibility PROPERTIES RUN_SERIAL TRUE TIMEOUT 600)

# Formatter behavior is covered by the dedicated formatter-layout regression
# below. The 1.0 source tree intentionally retains established formatting in
# older compiler modules; repository cleanup must not rewrite qualified compiler
# semantics merely to satisfy a whole-tree whitespace gate.

# C/C++ formatting is intentionally conservative: enforce readable vertical spacing
# between adjacent namespace-scope definitions without mass-reformatting expressions.
add_test(NAME raz-cpp-spacing-format
  COMMAND ${Python3_EXECUTABLE}
    ${PROJECT_SOURCE_DIR}/tools/format-cpp-spacing.py
    --check
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/tests)
set_tests_properties(raz-cpp-spacing-format PROPERTIES TIMEOUT 60)

# The production compiler is a canonical semantic module set. Bootstrap must
# discover it without imposing a physical source order or committing mirrors.
add_test(NAME raz-host-source-layout
  COMMAND ${CMAKE_COMMAND}
    -DPROJECT_ROOT=${PROJECT_SOURCE_DIR}
    -DBOOTSTRAP_SCRIPT=${PROJECT_SOURCE_DIR}/tools/bootstrap.py
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/host_source_layout.cmake)
set_tests_properties(raz-host-source-layout PROPERTIES TIMEOUT 30)

add_test(NAME raz-compiler-source-set
  COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/python/check-compiler-source-set.py)
set_tests_properties(raz-compiler-source-set PROPERTIES TIMEOUT 30)

add_test(NAME raz-formatter-scalar-parity
  COMMAND ${CMAKE_COMMAND}
    -DPYTHON_EXE=${Python3_EXECUTABLE}
    -DFORMATTER=${PROJECT_SOURCE_DIR}/tools/format-raz.py
    -DRAZC_EXE=$<TARGET_FILE:razc_host>
    -DWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/formatter-scalar-parity-work
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/formatter_scalar_parity.cmake)
set_tests_properties(raz-formatter-scalar-parity PROPERTIES TIMEOUT 30)

# Generic Drop monomorphization regression coverage.
add_test(NAME razc-check-generic-drop COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/ownership/generic_drop.rz)
add_test(NAME razc-hir-generic-drop COMMAND razc_host --hir ${PROJECT_SOURCE_DIR}/tests/examples/ownership/generic_drop.rz)
set_tests_properties(razc-hir-generic-drop PROPERTIES PASS_REGULAR_EXPRESSION "impl Drop for Box<Resource> using @__raz_drop_Box<Resource>_drop")
add_test(NAME razc-mir-generic-drop COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/ownership/generic_drop.rz)
set_tests_properties(razc-mir-generic-drop PROPERTIES PASS_REGULAR_EXPRESSION "fn @__raz_drop_Box<Resource>_drop;call void __raz_drop_Box<Resource>_drop;call void __raz_drop_Resource")
add_test(NAME razc-forge-generic-drop COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/ownership/generic_drop.rz)
set_tests_properties(razc-forge-generic-drop PROPERTIES PASS_REGULAR_EXPRESSION "func @__raz_drop_Box<Resource>_drop")
add_test(NAME razc-reject-generic-drop-signature COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/ownership/invalid_generic_drop_signature.rz)
set_tests_properties(razc-reject-generic-drop-signature PROPERTIES WILL_FAIL TRUE)
add_test(NAME razc-check-generic-clone-method COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/ownership/generic_clone_method.rz)
add_test(NAME razc-hir-generic-clone-method COMMAND razc_host --hir ${PROJECT_SOURCE_DIR}/tests/examples/ownership/generic_clone_method.rz)
set_tests_properties(razc-hir-generic-clone-method PROPERTIES PASS_REGULAR_EXPRESSION "impl Clone for Box<Token> using @__raz_clone_Box<Token>_clone")
add_test(NAME razc-mir-generic-clone-method COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/ownership/generic_clone_method.rz)
set_tests_properties(razc-mir-generic-clone-method PROPERTIES PASS_REGULAR_EXPRESSION "fn @__raz_clone_Box<Token>_clone;call Box<Token> __raz_clone_Box<Token>_clone;call Token __raz_clone_Token")
add_test(NAME razc-forge-generic-clone-method COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/ownership/generic_clone_method.rz)
set_tests_properties(razc-forge-generic-clone-method PROPERTIES PASS_REGULAR_EXPRESSION "func @__raz_clone_Box<Token>_clone")
add_test(NAME razc-reject-generic-clone-signature COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/ownership/invalid_generic_clone_signature.rz)
set_tests_properties(razc-reject-generic-clone-signature PROPERTIES WILL_FAIL TRUE)

# Generic inherent implementation and recursive structural Clone coverage.
add_test(NAME razc-check-generic-inherent-methods COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/generics/generic_inherent_methods.rz)
add_test(NAME razc-hir-generic-inherent-methods COMMAND razc_host --hir ${PROJECT_SOURCE_DIR}/tests/examples/generics/generic_inherent_methods.rz)
set_tests_properties(razc-hir-generic-inherent-methods PROPERTIES PASS_REGULAR_EXPRESSION "impl Box<i64> method get using @__raz_inherent_Box<i64>_get;impl Box<i64> method make using @__raz_inherent_Box<i64>_make")
add_test(NAME razc-mir-generic-inherent-methods COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/generics/generic_inherent_methods.rz)
set_tests_properties(razc-mir-generic-inherent-methods PROPERTIES PASS_REGULAR_EXPRESSION "call i64 __raz_inherent_Box<i64>_get;call Box<i64> __raz_inherent_Box<i64>_make")
add_test(NAME razc-forge-generic-inherent-methods COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/generics/generic_inherent_methods.rz)
set_tests_properties(razc-forge-generic-inherent-methods PROPERTIES PASS_REGULAR_EXPRESSION "func @__raz_inherent_Box<i64>_get;func @__raz_inherent_Box<i64>_make")
add_test(NAME razc-check-const-generic-inherent-methods COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/generics/const_generic_inherent_methods.rz)
add_test(NAME razc-mir-const-generic-inherent-methods COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/generics/const_generic_inherent_methods.rz)
set_tests_properties(razc-mir-const-generic-inherent-methods PROPERTIES PASS_REGULAR_EXPRESSION "fn @__raz_inherent_Buffer<i64,4>_width;const usize 4")
add_test(NAME razc-reject-generic-inherent-bound COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/generics/invalid_generic_inherent_bound.rz)
set_tests_properties(razc-reject-generic-inherent-bound PROPERTIES WILL_FAIL TRUE)
add_test(NAME razc-reject-overlapping-generic-inherent COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/generics/invalid_overlapping_generic_inherent.rz)
set_tests_properties(razc-reject-overlapping-generic-inherent PROPERTIES WILL_FAIL TRUE)

add_test(NAME razc-check-recursive-structural-clone COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/ownership/recursive_structural_clone.rz)
add_test(NAME razc-mir-recursive-structural-clone COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/ownership/recursive_structural_clone.rz)
set_tests_properties(razc-mir-recursive-structural-clone PROPERTIES PASS_REGULAR_EXPRESSION "call Token __raz_clone_Token")
add_test(NAME razc-forge-recursive-structural-clone COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/ownership/recursive_structural_clone.rz)
set_tests_properties(razc-forge-recursive-structural-clone PROPERTIES PASS_REGULAR_EXPRESSION "call ptr @__raz_clone_Token")
add_test(NAME razc-check-recursive-enum-clone COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/ownership/recursive_enum_clone.rz)
add_test(NAME razc-mir-recursive-enum-clone COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/ownership/recursive_enum_clone.rz)
set_tests_properties(razc-mir-recursive-enum-clone PROPERTIES PASS_REGULAR_EXPRESSION "clone.enum.payload;call Token __raz_clone_Token")
add_test(NAME razc-forge-recursive-enum-clone COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/examples/ownership/recursive_enum_clone.rz)
set_tests_properties(razc-forge-recursive-enum-clone PROPERTIES PASS_REGULAR_EXPRESSION "call ptr @__raz_clone_Token")

# Production standard-library OS boundary qualification.
add_executable(raz-stdlib-runtime-tests
    ${PROJECT_SOURCE_DIR}/tests/native/runtime/stdlib_runtime_tests.cpp)
target_link_libraries(raz-stdlib-runtime-tests PRIVATE Raz::runtime)
add_test(NAME raz-stdlib-runtime-tests COMMAND raz-stdlib-runtime-tests)

add_test(NAME raz-stdlib-execution
  COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/python/test-stdlib-execution.py
    --razc $<TARGET_FILE:razc_host>
    --forge-codegen $<TARGET_FILE:raz-compiler-forge-codegen>
    --runtime $<TARGET_FILE:raz_runtime>
    --runtime-link-manifest ${CMAKE_BINARY_DIR}/raz-runtime-link-deps.txt
    --timeout 60)
set_tests_properties(raz-stdlib-execution PROPERTIES RUN_SERIAL TRUE TIMEOUT 180)

# Source-level package/module namespace isolation and collection coverage.
add_test(NAME razc-check-namespace-overlap
  COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/namespaces/overlap_smoke.rz)
add_test(NAME razc-forge-namespace-overlap
  COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/namespaces/overlap_smoke.rz)
set_tests_properties(razc-forge-namespace-overlap PROPERTIES
  PASS_REGULAR_EXPRESSION "func @__raz_ns_alpha__storage__answer;func @__raz_ns_beta__storage__answer;call i64 @__raz_ns_alpha__storage__answer")
add_test(NAME razc-reject-ambiguous-namespace-import
  COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/namespaces/ambiguous_import.rz)
set_tests_properties(razc-reject-ambiguous-namespace-import PROPERTIES WILL_FAIL TRUE)

add_test(NAME razc-check-library-deque
  COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/library/alloc/deque/deque.rz)
add_test(NAME razc-check-library-hash-set
  COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/library/alloc/hash_set/hash_set.rz)
add_test(NAME razc-check-library-hash-map
  COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/library/alloc/hash_map/hash_map.rz)

add_test(NAME raz-project-namespace-overlap
  COMMAND raz_host run ${PROJECT_SOURCE_DIR}/tests/examples/projects/namespace-consumer --target test-host --force)
set_tests_properties(raz-project-namespace-overlap PROPERTIES RUN_SERIAL TRUE TIMEOUT 30)

# Production package/module semantics.
add_test(NAME raz-project-package-api-reexport
  COMMAND raz_host run ${PROJECT_SOURCE_DIR}/tests/examples/projects/package-api-consumer --target test-host --force)
set_tests_properties(raz-project-package-api-reexport PROPERTIES RUN_SERIAL TRUE TIMEOUT 30)

add_test(NAME raz-project-package-internal-local
  COMMAND raz_host run ${PROJECT_SOURCE_DIR}/tests/examples/projects/package-local-package --target test-host --force)
set_tests_properties(raz-project-package-internal-local PROPERTIES RUN_SERIAL TRUE TIMEOUT 30)

add_test(NAME raz-project-reject-package-internal-cross-package
  COMMAND raz_host check ${PROJECT_SOURCE_DIR}/tests/examples/projects/package-api-private-consumer --target test-host --force)
set_tests_properties(raz-project-reject-package-internal-cross-package PROPERTIES WILL_FAIL TRUE RUN_SERIAL TRUE TIMEOUT 30)

add_test(NAME raz-project-reject-private-cross-module
  COMMAND raz_host check ${PROJECT_SOURCE_DIR}/tests/examples/projects/package-local-private --target test-host --force)
set_tests_properties(raz-project-reject-private-cross-module PROPERTIES WILL_FAIL TRUE RUN_SERIAL TRUE TIMEOUT 30)

add_test(NAME raz-project-reject-nonentry-main
  COMMAND raz_host check ${PROJECT_SOURCE_DIR}/tests/examples/projects/package-entry-ownership --target test-host --force)
set_tests_properties(raz-project-reject-nonentry-main PROPERTIES WILL_FAIL TRUE RUN_SERIAL TRUE TIMEOUT 30)

add_test(NAME raz-project-reject-package-identity-conflict
  COMMAND raz_host check ${PROJECT_SOURCE_DIR}/tests/examples/projects/package-conflict --target test-host --force)
set_tests_properties(raz-project-reject-package-identity-conflict PROPERTIES WILL_FAIL TRUE RUN_SERIAL TRUE TIMEOUT 30)

add_test(NAME razc-reject-ambiguous-import-alias
  COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/namespaces/ambiguous_alias.rz)
set_tests_properties(razc-reject-ambiguous-import-alias PROPERTIES WILL_FAIL TRUE)

add_test(NAME raz-package-semantics-audit
  COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/python/check-package-semantics.py)

add_test(NAME raz-project-reject-dependency-cycle
  COMMAND raz_host check ${PROJECT_SOURCE_DIR}/tests/examples/projects/package-cycle-a --target test-host --force)
set_tests_properties(raz-project-reject-dependency-cycle PROPERTIES WILL_FAIL TRUE RUN_SERIAL TRUE TIMEOUT 30)

add_test(NAME raz-project-reject-duplicate-module-namespace
  COMMAND raz_host check ${PROJECT_SOURCE_DIR}/tests/examples/projects/package-duplicate-module --target test-host --force)
set_tests_properties(raz-project-reject-duplicate-module-namespace PROPERTIES WILL_FAIL TRUE RUN_SERIAL TRUE TIMEOUT 30)

# Generic typed collections and namespace-safe unified iteration.
foreach(collection_case vector deque hash_set hash_map)
  add_test(NAME raz-generic-collection-${collection_case}-forge
    COMMAND ${CMAKE_COMMAND}
      -DRAZC_EXE=$<TARGET_FILE:razc_host>
      -DSOURCE_ROOT=${PROJECT_SOURCE_DIR}
      -DWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/generic-collection-${collection_case}-work
      -DCASE=${collection_case}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/generic_collections_runtime.cmake)
  set_tests_properties(raz-generic-collection-${collection_case}-forge PROPERTIES TIMEOUT 30)
endforeach()

add_test(NAME raz-unsafe-raw-storage
  COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/collections/unsafe_raw_storage.rz)
add_test(NAME raz-reject-safe-raw-cast
  COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/collections/invalid_safe_raw_cast.rz)
set_tests_properties(raz-reject-safe-raw-cast PROPERTIES WILL_FAIL TRUE)

add_test(NAME raz-generic-collections-project-runtime
  COMMAND ${CMAKE_COMMAND}
    -DRAZ_EXE=$<TARGET_FILE:raz_host>
    -DSOURCE_ROOT=${PROJECT_SOURCE_DIR}
    -DWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/generic-collections-project-runtime-work
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/project_runtime.cmake)
set_tests_properties(raz-generic-collections-project-runtime PROPERTIES RUN_SERIAL TRUE TIMEOUT 60)

add_test(NAME raz-generic-collections-cross-package-runtime
  COMMAND ${CMAKE_COMMAND}
    -DRAZ_EXE=$<TARGET_FILE:raz_host>
    -DSOURCE_ROOT=${PROJECT_SOURCE_DIR}
    -DWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/generic-collections-cross-package-runtime-work
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/cross_package_runtime.cmake)
set_tests_properties(raz-generic-collections-cross-package-runtime PROPERTIES RUN_SERIAL TRUE TIMEOUT 60)


# Structured error propagation and deterministic early-return cleanup.
add_test(NAME raz-error-flexible-propagation-check
  COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/error_handling/flexible_propagation.rz)
add_test(NAME raz-error-flexible-propagation-forge
  COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/error_handling/flexible_propagation.rz)
set_tests_properties(raz-error-flexible-propagation-forge PROPERTIES
  PASS_REGULAR_EXPRESSION "Result<bool,i64>;Result<i64,i64>;Option<bool>;Option<i64>")

add_test(NAME raz-reject-incompatible-error-channel
  COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/error_handling/invalid_error_channel.rz)
set_tests_properties(raz-reject-incompatible-error-channel PROPERTIES WILL_FAIL TRUE)

add_test(NAME raz-error-resource-runtime
  COMMAND ${CMAKE_COMMAND}
    -DRAZ_EXE=$<TARGET_FILE:raz_host>
    -DSOURCE_ROOT=${PROJECT_SOURCE_DIR}
    -DWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/error-resource-runtime-work
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/error_resource_runtime.cmake)
set_tests_properties(raz-error-resource-runtime PROPERTIES RUN_SERIAL TRUE TIMEOUT 60)

add_test(NAME raz-cross-package-error-runtime
  COMMAND ${CMAKE_COMMAND}
    -DRAZ_EXE=$<TARGET_FILE:raz_host>
    -DSOURCE_ROOT=${PROJECT_SOURCE_DIR}
    -DWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/cross-package-error-runtime-work
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/cross_package_error_runtime.cmake)
set_tests_properties(raz-cross-package-error-runtime PROPERTIES RUN_SERIAL TRUE TIMEOUT 60)

add_test(NAME raz-forge-aggregate-roundtrip
  COMMAND ${CMAKE_COMMAND}
    -DRAZ=$<TARGET_FILE:raz_host>
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/forge-aggregate-roundtrip-fixture
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/forge_aggregate_roundtrip.cmake)

# Concrete generic layout ABI and arbitrary-width inline collection storage.
add_test(NAME raz-generic-layout-forge
  COMMAND razc_host --forge-ir ${PROJECT_SOURCE_DIR}/tests/layout/generic_layout.rz)
set_tests_properties(raz-generic-layout-forge PROPERTIES
  PASS_REGULAR_EXPRESSION "const i64 24;const i64 32")

add_test(NAME raz-wide-layout-runtime
  COMMAND ${CMAKE_COMMAND}
    -DRAZ_EXE=$<TARGET_FILE:raz_host>
    -DSOURCE_ROOT=${PROJECT_SOURCE_DIR}
    -DWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/wide-layout-runtime-work
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/wide_layout_runtime.cmake)
set_tests_properties(raz-wide-layout-runtime PROPERTIES RUN_SERIAL TRUE TIMEOUT 90)

add_test(NAME raz-cross-package-layout-runtime
  COMMAND ${CMAKE_COMMAND}
    -DRAZ_EXE=$<TARGET_FILE:raz_host>
    -DWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/cross-package-layout-runtime-work
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/cross_package_layout_runtime.cmake)
set_tests_properties(raz-cross-package-layout-runtime PROPERTIES RUN_SERIAL TRUE TIMEOUT 60)
add_test(NAME raz-generic-layout-audit
  COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/python/check-generic-layout.py)

# Production standard-library breadth and collection ergonomics.
add_test(NAME raz-return-move-runtime
  COMMAND ${CMAKE_COMMAND}
    -DRAZ_EXE=$<TARGET_FILE:raz_host>
    -DWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/return-move-runtime-work
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/return_move_runtime.cmake)
set_tests_properties(raz-return-move-runtime PROPERTIES RUN_SERIAL TRUE TIMEOUT 60)

add_test(NAME raz-collection-ergonomics-runtime
  COMMAND ${CMAKE_COMMAND}
    -DRAZ_EXE=$<TARGET_FILE:raz_host>
    -DSOURCE_ROOT=${PROJECT_SOURCE_DIR}
    -DWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/collection-ergonomics-runtime-work
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/collection_ergonomics_runtime.cmake)
set_tests_properties(raz-collection-ergonomics-runtime PROPERTIES RUN_SERIAL TRUE TIMEOUT 90)

add_test(NAME raz-core-ergonomics-runtime
  COMMAND ${CMAKE_COMMAND}
    -DRAZ_EXE=$<TARGET_FILE:raz_host>
    -DSOURCE_ROOT=${PROJECT_SOURCE_DIR}
    -DWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/core-ergonomics-runtime-work
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/core_ergonomics_runtime.cmake)
set_tests_properties(raz-core-ergonomics-runtime PROPERTIES RUN_SERIAL TRUE TIMEOUT 90)

add_test(NAME raz-owned-environment-runtime
  COMMAND ${CMAKE_COMMAND}
    -DRAZ_EXE=$<TARGET_FILE:raz_host>
    -DPYTHON_EXECUTABLE=${Python3_EXECUTABLE}
    -DSOURCE_ROOT=${PROJECT_SOURCE_DIR}
    -DWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/owned-environment-runtime-work
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/owned_environment_runtime.cmake)
set_tests_properties(raz-owned-environment-runtime PROPERTIES RUN_SERIAL TRUE TIMEOUT 90)

add_test(NAME raz-owned-io-runtime
  COMMAND ${CMAKE_COMMAND}
    -DRAZ_EXE=$<TARGET_FILE:raz_host>
    -DPYTHON_EXECUTABLE=${Python3_EXECUTABLE}
    -DSOURCE_ROOT=${PROJECT_SOURCE_DIR}
    -DWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/owned-io-runtime-work
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/owned_io_runtime.cmake)
set_tests_properties(raz-owned-io-runtime PROPERTIES RUN_SERIAL TRUE TIMEOUT 90)

add_test(NAME raz-random-module-check
  COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/library/std/random/random.rz)
add_test(NAME raz-stdlib-ergonomics-audit
  COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/python/check-stdlib-ergonomics.py)
add_test(NAME raz-stdlib-performance-audit
  COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/python/check-stdlib-performance.py)

add_test(NAME raz-stdlib-systems-runtime
  COMMAND ${CMAKE_COMMAND}
    -DRAZ_EXE=$<TARGET_FILE:raz_host>
    -DPYTHON_EXECUTABLE=${Python3_EXECUTABLE}
    -DSOURCE_ROOT=${PROJECT_SOURCE_DIR}
    -DWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/stdlib-systems-runtime-work
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/stdlib_systems_runtime.cmake)
set_tests_properties(raz-stdlib-systems-runtime PROPERTIES RUN_SERIAL TRUE TIMEOUT 180)

# Production networking, serialization, and higher-level I/O.
add_test(NAME raz-network-codec-runtime
  COMMAND ${CMAKE_COMMAND}
    -DRAZ_EXE=$<TARGET_FILE:raz_host>
    -DSOURCE_ROOT=${PROJECT_SOURCE_DIR}
    -DWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/network-codec-runtime-work
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/network_codec_runtime.cmake)
set_tests_properties(raz-network-codec-runtime PROPERTIES RUN_SERIAL TRUE TIMEOUT 120)

add_test(NAME raz-cross-package-codec-runtime
  COMMAND ${CMAKE_COMMAND}
    -DRAZ_EXE=$<TARGET_FILE:raz_host>
    -DSOURCE_ROOT=${PROJECT_SOURCE_DIR}
    -DWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/cross-package-codec-runtime-work
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/cross_package_codec_runtime.cmake)
set_tests_properties(raz-cross-package-codec-runtime PROPERTIES RUN_SERIAL TRUE TIMEOUT 90)

add_test(NAME raz-hex-fir-roundtrip
  COMMAND ${CMAKE_COMMAND}
    -DRAZ_EXE=$<TARGET_FILE:raz_host>
    -DWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/hex-fir-roundtrip-work
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/hex_fir_roundtrip.cmake)
set_tests_properties(raz-hex-fir-roundtrip PROPERTIES RUN_SERIAL TRUE TIMEOUT 60)

add_test(NAME raz-network-serialization-audit
  COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/python/check-network-serialization.py)

# Pure-Raz encoding utilities and borrowed URL parsing.
add_test(NAME raz-stdlib-codecs-runtime
  COMMAND ${CMAKE_COMMAND}
    -DRAZ_EXE=$<TARGET_FILE:raz_host>
    -DSOURCE_ROOT=${PROJECT_SOURCE_DIR}
    -DWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/stdlib-codecs-runtime-work
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/stdlib_codecs_runtime.cmake)
set_tests_properties(raz-stdlib-codecs-runtime PROPERTIES RUN_SERIAL TRUE TIMEOUT 90)
add_test(NAME raz-stdlib-codecs-audit
  COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/python/check-stdlib-codecs.py)

add_test(NAME raz-core-stdlib-runtime
  COMMAND ${CMAKE_COMMAND}
    -DRAZ_EXE=$<TARGET_FILE:raz_host>
    -DPYTHON_EXECUTABLE=${Python3_EXECUTABLE}
    -DSOURCE_ROOT=${PROJECT_SOURCE_DIR}
    -DWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/core-stdlib-runtime-work
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/core_stdlib_runtime.cmake)
set_tests_properties(raz-core-stdlib-runtime PROPERTIES RUN_SERIAL TRUE TIMEOUT 120)
add_test(NAME raz-hosted-os-runtime
  COMMAND ${CMAKE_COMMAND}
    -DRAZ_EXE=$<TARGET_FILE:raz_host>
    -DPYTHON_EXECUTABLE=${Python3_EXECUTABLE}
    -DSOURCE_ROOT=${PROJECT_SOURCE_DIR}
    -DWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/hosted-os-runtime-work
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/hosted_os_runtime.cmake)
set_tests_properties(raz-hosted-os-runtime PROPERTIES RUN_SERIAL TRUE TIMEOUT 120)

add_test(NAME raz-network-concurrency-runtime
  COMMAND ${CMAKE_COMMAND}
    -DRAZ_EXE=$<TARGET_FILE:raz_host>
    -DPYTHON_EXECUTABLE=${Python3_EXECUTABLE}
    -DSOURCE_ROOT=${PROJECT_SOURCE_DIR}
    -DWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/network-concurrency-runtime-work
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/network_concurrency_runtime.cmake)
set_tests_properties(raz-network-concurrency-runtime PROPERTIES RUN_SERIAL TRUE TIMEOUT 120)

add_test(NAME raz-async-runtime
  COMMAND ${CMAKE_COMMAND}
    -DRAZ_EXE=$<TARGET_FILE:raz_host>
    -DPYTHON_EXECUTABLE=${Python3_EXECUTABLE}
    -DSOURCE_ROOT=${PROJECT_SOURCE_DIR}
    -DWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/async-runtime-work
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/async_runtime.cmake)
set_tests_properties(raz-async-runtime PROPERTIES RUN_SERIAL TRUE TIMEOUT 120)

add_test(NAME raz-callable-runtime
  COMMAND ${CMAKE_COMMAND}
    -DRAZ_EXE=$<TARGET_FILE:raz_host>
    -DPYTHON_EXECUTABLE=${Python3_EXECUTABLE}
    -DSOURCE_ROOT=${PROJECT_SOURCE_DIR}
    -DWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/callable-runtime-work
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/callable_runtime.cmake)
set_tests_properties(raz-callable-runtime PROPERTIES RUN_SERIAL TRUE TIMEOUT 120)

add_test(NAME raz-scheduler-stress
  COMMAND ${CMAKE_COMMAND}
    -DRAZ_EXE=$<TARGET_FILE:raz_host>
    -DPYTHON_EXECUTABLE=${Python3_EXECUTABLE}
    -DSOURCE_ROOT=${PROJECT_SOURCE_DIR}
    -DWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/scheduler-stress-work
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/scheduler_stress.cmake)
set_tests_properties(raz-scheduler-stress PROPERTIES RUN_SERIAL TRUE TIMEOUT 120)

add_test(NAME raz-application-stack-runtime
  COMMAND ${CMAKE_COMMAND}
    -DRAZ_EXE=$<TARGET_FILE:raz_host>
    -DPYTHON_EXECUTABLE=${Python3_EXECUTABLE}
    -DSOURCE_ROOT=${PROJECT_SOURCE_DIR}
    -DWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/application-stack-runtime-work
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/application_stack_runtime.cmake)
set_tests_properties(raz-application-stack-runtime PROPERTIES RUN_SERIAL TRUE TIMEOUT 180)


add_test(NAME raz-high-level-application-runtime
  COMMAND ${CMAKE_COMMAND}
    -DRAZ_EXE=$<TARGET_FILE:raz_host>
    -DPYTHON_EXECUTABLE=${Python3_EXECUTABLE}
    -DSOURCE_ROOT=${PROJECT_SOURCE_DIR}
    -DWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/high-level-application-runtime-work
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/high_level_application_runtime.cmake)
set_tests_properties(raz-high-level-application-runtime PROPERTIES RUN_SERIAL TRUE TIMEOUT 180)

add_test(NAME raz-http-zero-copy-runtime
  COMMAND ${CMAKE_COMMAND}
    -DRAZ_EXE=$<TARGET_FILE:raz_host>
    -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
    -DPYTHON_EXECUTABLE=${Python3_EXECUTABLE}
    -DWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/http-zero-copy-runtime-work
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/http_zero_copy_runtime.cmake)
set_tests_properties(raz-http-zero-copy-runtime PROPERTIES RUN_SERIAL TRUE TIMEOUT 180)

add_test(NAME raz-compression-runtime
  COMMAND ${CMAKE_COMMAND}
    -DRAZ_EXE=$<TARGET_FILE:raz_host>
    -DPYTHON_EXECUTABLE=${Python3_EXECUTABLE}
    -DSOURCE_ROOT=${PROJECT_SOURCE_DIR}
    -DWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/compression-runtime-work
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/compression_runtime.cmake)
set_tests_properties(raz-compression-runtime PROPERTIES RUN_SERIAL TRUE TIMEOUT 180)

# Primitive integer associated constants are compiler intrinsics. This
# regression protects the former semantic-analysis blow-up around expressions
# such as `(i64::MAX - digit) / radix` and verifies the common integer widths.
add_test(NAME razc-check-primitive-integer-constants COMMAND razc_host --check ${PROJECT_SOURCE_DIR}/tests/examples/compiler/primitive_integer_constants.rz)
add_test(NAME razc-mir-primitive-integer-constants COMMAND razc_host --mir ${PROJECT_SOURCE_DIR}/tests/examples/compiler/primitive_integer_constants.rz)
set_tests_properties(razc-mir-primitive-integer-constants PROPERTIES PASS_REGULAR_EXPRESSION "9223372036854775807")

# Memoized reflection layout computation and unified semantic-query profiling.
add_test(NAME raz-layout-query-profile
  COMMAND ${CMAKE_COMMAND}
    -DRAZ_EXE=$<TARGET_FILE:raz_host>
    -DSOURCE_ROOT=${PROJECT_SOURCE_DIR}
    -DWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/layout-query-profile-work
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/query_profile.cmake)
set_tests_properties(raz-layout-query-profile PROPERTIES RUN_SERIAL TRUE TIMEOUT 90)

# Native platform policy: LLVM AArch64 targets, host-aware backend default,
# cross-target runtime safety, and AArch64 stage-0 bootstrap contract.
add_test(NAME raz-platform-expansion-source
  COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/python/check-platform-expansion.py)
set_tests_properties(raz-platform-expansion-source PROPERTIES TIMEOUT 30)

# Cross-stage AArch64 object qualification: Raz frontend -> canonical Forge IR ->
# embedded Forge AArch64 ELF/Mach-O writers. This is executable on x86-64 CI and
# verifies architecture/object-format identity without pretending to execute ARM code.
add_test(NAME raz-forge-aarch64-cross-object
  COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/python/test-forge-aarch64-cross-object.py
    --razc $<TARGET_FILE:razc_host>
    --forge-codegen $<TARGET_FILE:raz-compiler-forge-codegen>
    --work ${CMAKE_CURRENT_BINARY_DIR}/forge-aarch64-cross-object-work)
set_tests_properties(raz-forge-aarch64-cross-object PROPERTIES RUN_SERIAL TRUE TIMEOUT 60)

# Production compiler CLI, discoverable Forge/LLVM backend selection,
# command help, target/backend discovery, and toolchain diagnostics.
add_test(NAME raz-cli-capabilities
  COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/python/check-cli-capabilities.py
    --raz $<TARGET_FILE:raz_host>
    --root ${PROJECT_SOURCE_DIR}
    --work ${CMAKE_CURRENT_BINARY_DIR}/cli-capabilities-work
    --linker ${CMAKE_CXX_COMPILER})
set_tests_properties(raz-cli-capabilities PROPERTIES RUN_SERIAL TRUE TIMEOUT 120)

# Production source tooling -- deterministic formatting, discovered
# test execution, and Markdown API extraction from /// documentation comments.
add_test(NAME raz-compiler-tooling
  COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/python/check-compiler-tooling.py
    --raz $<TARGET_FILE:raz_host>
    --root ${PROJECT_SOURCE_DIR}
    --work ${CMAKE_CURRENT_BINARY_DIR}/compiler-tooling-work
    --linker ${CMAKE_CXX_COMPILER})
set_tests_properties(raz-compiler-tooling PROPERTIES RUN_SERIAL TRUE TIMEOUT 120)

add_test(NAME raz-package-manager
  COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/python/check-package-manager.py
    --raz $<TARGET_FILE:raz_host>
    --root ${PROJECT_SOURCE_DIR}
    --work ${CMAKE_CURRENT_BINARY_DIR}/package-manager-work
    --linker ${CMAKE_CXX_COMPILER})
set_tests_properties(raz-package-manager PROPERTIES RUN_SERIAL TRUE TIMEOUT 600)

add_test(NAME raz-dependency-semantics
  COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/python/check-dependency-semantics.py
    --raz $<TARGET_FILE:raz_host>
    --root ${PROJECT_SOURCE_DIR}
    --work ${CMAKE_CURRENT_BINARY_DIR}/dependency-semantics-work
    --linker ${CMAKE_CXX_COMPILER})
set_tests_properties(raz-dependency-semantics PROPERTIES RUN_SERIAL TRUE TIMEOUT 240)

add_test(NAME raz-package-workspaces
  COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/python/check-package-workspaces.py
    --raz $<TARGET_FILE:raz_host>
    --root ${PROJECT_SOURCE_DIR}
    --work ${CMAKE_CURRENT_BINARY_DIR}/package-workspaces-work
    --linker ${CMAKE_CXX_COMPILER})
set_tests_properties(raz-package-workspaces PROPERTIES RUN_SERIAL TRUE TIMEOUT 240)

# Official GitHub-backed registry defaults and one-argument package-add syntax.
add_test(NAME raz-official-registry
  COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/python/check-official-registry.py)
set_tests_properties(raz-official-registry PROPERTIES TIMEOUT 30)

# Read-only registry discovery and upgrade reporting use the static index without
# materializing package archives.
add_test(NAME raz-registry-discovery
  COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/python/check-registry-discovery.py
    --raz $<TARGET_FILE:raz_host>
    --root ${PROJECT_SOURCE_DIR}
    --work ${CMAKE_CURRENT_BINARY_DIR}/registry-discovery-work
    --linker ${CMAKE_CXX_COMPILER})
set_tests_properties(raz-registry-discovery PROPERTIES RUN_SERIAL TRUE TIMEOUT 90)

# Registry resolution, semver selection, integrity checks, and deterministic package-store reuse.
add_test(NAME raz-registry-package-store
  COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/python/check-registry-package-store.py
    --raz $<TARGET_FILE:raz_host>
    --root ${PROJECT_SOURCE_DIR}
    --work ${CMAKE_CURRENT_BINARY_DIR}/registry-package-store-work
    --linker ${CMAKE_CXX_COMPILER})
set_tests_properties(raz-registry-package-store PROPERTIES RUN_SERIAL TRUE TIMEOUT 600)

add_test(NAME raz-registry-network-store
  COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/python/check-registry-network-store.py
    --raz $<TARGET_FILE:raz_host>
    --root ${PROJECT_SOURCE_DIR}
    --work ${CMAKE_CURRENT_BINARY_DIR}/registry-network-store-work
    --linker ${CMAKE_CXX_COMPILER})
set_tests_properties(raz-registry-network-store PROPERTIES RUN_SERIAL TRUE TIMEOUT 180)

add_test(NAME raz-registry-publishing
  COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/python/check-registry-publishing.py
    --raz $<TARGET_FILE:raz_host>
    --root ${PROJECT_SOURCE_DIR}
    --work ${CMAKE_CURRENT_BINARY_DIR}/registry-publishing-work
    --linker ${CMAKE_CXX_COMPILER})
set_tests_properties(raz-registry-publishing PROPERTIES RUN_SERIAL TRUE TIMEOUT 180)

add_test(NAME raz-registry-signing-parallel
  COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/python/check-registry-signing-parallel.py
    --raz $<TARGET_FILE:raz_host>
    --root ${PROJECT_SOURCE_DIR}
    --work ${CMAKE_CURRENT_BINARY_DIR}/registry-signing-parallel-work
    --linker ${CMAKE_CXX_COMPILER})
set_tests_properties(raz-registry-signing-parallel PROPERTIES RUN_SERIAL TRUE TIMEOUT 240)

# Human-facing command output and structured source diagnostics. Keep color
# optional/controllable for CI while preserving clean terminal presentation.
add_test(NAME raz-cli-output-diagnostics
  COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/python/check-cli-output-diagnostics.py
    --raz $<TARGET_FILE:raz_host>
    --razc $<TARGET_FILE:razc_host>
    --work ${CMAKE_CURRENT_BINARY_DIR}/cli-output-diagnostics-work)
set_tests_properties(raz-cli-output-diagnostics PROPERTIES RUN_SERIAL TRUE TIMEOUT 120)

add_test(NAME raz-project-native-output
  COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/python/check-project-native-output.py)
set_tests_properties(raz-project-native-output PROPERTIES TIMEOUT 30)

add_test(NAME raz-bootstrap-runtime-deps
  COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/python/check-bootstrap-runtime-deps.py)
set_tests_properties(raz-bootstrap-runtime-deps PROPERTIES TIMEOUT 30)

add_test(NAME raz-sync-policy-runtime
  COMMAND ${CMAKE_COMMAND}
    -DRAZ_EXE=$<TARGET_FILE:raz_host>
    -DPYTHON_EXECUTABLE=${Python3_EXECUTABLE}
    -DSOURCE_ROOT=${PROJECT_SOURCE_DIR}
    -DWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/sync-policy-runtime-work
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/sync_policy_runtime.cmake)
set_tests_properties(raz-sync-policy-runtime PROPERTIES RUN_SERIAL TRUE TIMEOUT 120)

add_test(NAME raz-workspace-graph
  COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/python/check-workspace-graph.py
    --raz $<TARGET_FILE:raz_host>
    --fixture ${PROJECT_SOURCE_DIR}/tests/examples/projects/hello-package
    --work ${CMAKE_CURRENT_BINARY_DIR}/workspace-graph-work)
set_tests_properties(raz-workspace-graph PROPERTIES RUN_SERIAL TRUE TIMEOUT 60)

add_test(NAME raz-three-pass-incremental
  COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/python/check-incremental-stability.py
    --raz $<TARGET_FILE:raz_host>
    --fixture ${PROJECT_SOURCE_DIR}/tests/examples/projects/hello-package
    --work ${CMAKE_CURRENT_BINARY_DIR}/three-pass-incremental-work)
set_tests_properties(raz-three-pass-incremental PROPERTIES RUN_SERIAL TRUE TIMEOUT 60)

# Repository licensing is a release invariant: every maintained source/build/script
# file carries SPDX metadata and the root legal files must remain present.
add_test(NAME raz-license-headers
  COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/python/check-license-headers.py)
set_tests_properties(raz-license-headers PROPERTIES RUN_SERIAL TRUE TIMEOUT 30)

add_test(NAME raz-repository-hygiene
  COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/python/check-repository-hygiene.py)
set_tests_properties(raz-repository-hygiene PROPERTIES RUN_SERIAL TRUE TIMEOUT 30)

add_test(NAME raz-test-layout
  COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/python/check-test-layout.py)
set_tests_properties(raz-test-layout PROPERTIES RUN_SERIAL TRUE TIMEOUT 30)

add_test(NAME raz-rebrand
  COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/python/check-raz-rebrand.py)
set_tests_properties(raz-rebrand PROPERTIES RUN_SERIAL TRUE TIMEOUT 30)

add_test(NAME raz-formatter-layout
  COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/python/check-raz-formatter-layout.py)
set_tests_properties(raz-formatter-layout PROPERTIES RUN_SERIAL TRUE TIMEOUT 30)

add_test(NAME razc-multiline-trailing-commas
  COMMAND $<TARGET_FILE:razc_host> --check ${PROJECT_SOURCE_DIR}/tests/format/multiline_trailing_commas.rz)
set_tests_properties(razc-multiline-trailing-commas PROPERTIES TIMEOUT 30)

add_test(NAME raz-verified-mir-audit
  COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/python/check-verified-mir.py)
set_tests_properties(raz-verified-mir-audit PROPERTIES RUN_SERIAL TRUE TIMEOUT 30)

add_test(NAME raz-trait-solver-audit
  COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/python/check-trait-solver.py)
set_tests_properties(raz-trait-solver-audit PROPERTIES RUN_SERIAL TRUE TIMEOUT 30)

add_test(NAME raz-host-compiler-contract
  COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/python/check-host-compiler-contract.py)
set_tests_properties(raz-host-compiler-contract PROPERTIES RUN_SERIAL TRUE TIMEOUT 30)

add_test(NAME raz-stage0-boundary
  COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/python/check-stage0-boundary.py)
set_tests_properties(raz-stage0-boundary PROPERTIES RUN_SERIAL TRUE TIMEOUT 30)

add_test(NAME raz-stage0-cache
  COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/python/check-stage0-cache.py)
set_tests_properties(raz-stage0-cache PROPERTIES RUN_SERIAL TRUE TIMEOUT 30)

add_test(NAME raz-bootstrap-fixed-point
  COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/python/check-bootstrap-fixed-point.py)
set_tests_properties(raz-bootstrap-fixed-point PROPERTIES RUN_SERIAL TRUE TIMEOUT 30)

add_test(NAME raz-compiler-trait-solver
  COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/python/check-compiler-trait-solver.py
    --raz $<TARGET_FILE:raz_host>
    --root ${PROJECT_SOURCE_DIR}
    --work ${CMAKE_CURRENT_BINARY_DIR}/compiler-trait-solver
    --linker ${CMAKE_CXX_COMPILER})
set_tests_properties(raz-compiler-trait-solver PROPERTIES RUN_SERIAL TRUE TIMEOUT 120)

add_test(NAME raz-toolchain-manager
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/python/check-razup.py)
set_tests_properties(raz-toolchain-manager PROPERTIES RUN_SERIAL TRUE TIMEOUT 30)

add_test(NAME raz-toolchain-manager-runtime
  COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/python/check-razup-runtime.py
    --raz $<TARGET_FILE:raz_host>
    --root ${PROJECT_SOURCE_DIR}
    --work ${CMAKE_CURRENT_BINARY_DIR}/razup-runtime-work
    --linker ${CMAKE_CXX_COMPILER})
set_tests_properties(raz-toolchain-manager-runtime PROPERTIES RUN_SERIAL TRUE TIMEOUT 240)

add_test(NAME compiler-text-literals
  COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/python/check-compiler-text-literals.py)
