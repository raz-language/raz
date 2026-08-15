// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge-c/forge.h"
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {
int fail(const char* message) {
    std::cerr << message << ": " << forge_last_error() << '\n';
    return 1;
}
}

int main() {
    if (FORGE_C_API_VERSION != 14) return fail("unexpected C API version");
    auto* context = forge_context_create();
    auto* module = forge_module_create(context, "c_api_test");
    const forge_type_kind_t parameters[] = {FORGE_TYPE_I64, FORGE_TYPE_I64};
    auto* add = forge_function_create(module, "add", FORGE_TYPE_I64, parameters, 2);
    auto* entry = forge_block_create(add, "entry");
    if (!forge_function_set_abi(add, FORGE_CALL_C, 0, FORGE_LINKAGE_EXTERNAL, FORGE_VISIBILITY_HIDDEN))
        return fail("failed to configure function ABI");

    // Create another function after the first handles to prove index-backed handles survive vector growth.
    auto* caller = forge_function_create(module, "caller", FORGE_TYPE_I64, nullptr, 0);
    auto* caller_entry = forge_block_create(caller, "entry");
    auto* builder = forge_builder_create(context, module);

    forge_builder_position_at_end(builder, entry);
    forge_builder_set_source_range(builder, "sample.c", 10, 2, 10, 14);
    forge_module_set_metadata(module, "frontend.language", "C");
    forge_builder_set_next_attribute(builder, "frontend.ast_id", "7");
    const char* lhs = forge_function_parameter(add, 0);
    const char* rhs = forge_function_parameter(add, 1);
    const char* sum = forge_builder_binary(builder, FORGE_OPCODE_ADD, FORGE_TYPE_I64, lhs, rhs);
    if (sum == nullptr) return fail("failed to build add");
    if (std::strcmp(forge_module_get_metadata(module, "frontend.language"), "C") != 0)
        return fail("module metadata mismatch");
    forge_builder_return(builder, sum);
    if (!forge_builder_has_insertion_point(builder) || !forge_builder_insertion_block_terminated(builder))
        return fail("builder state query failed");

    forge_builder_position_at_end(builder, caller_entry);
    const char* left = forge_builder_constant(builder, FORGE_TYPE_I64, "20");
    const std::string left_copy = left ? left : "";
    const char* right = forge_builder_constant(builder, FORGE_TYPE_I64, "22");
    const std::string right_copy = right ? right : "";
    const char* arguments[] = {left_copy.c_str(), right_copy.c_str()};
    const char* result = forge_builder_call(builder, FORGE_TYPE_I64, "add", arguments, 2);
    if (result == nullptr) return fail("failed to build call");
    forge_builder_return(builder, result);

    char error[256]{};
    if (forge_module_verify(module, error, sizeof(error)) != 1) {
        std::cerr << "verification failed: " << error << '\n';
        return 1;
    }
    const size_t required = forge_module_print(module, nullptr, 0);
    if (required < 2) return fail("failed to size printed module");
    std::vector<char> text(required);
    if (forge_module_print(module, text.data(), text.size()) != required) return fail("failed to print module");
    if (std::strstr(text.data(), "call i64 @add") == nullptr) return fail("printed module omitted call");
    const size_t map_required = forge_module_source_map_json(module, nullptr, 0);
    if (map_required < 2) return fail("failed to size source map");
    std::vector<char> source_map(map_required);
    if (forge_module_source_map_json(module, source_map.data(), source_map.size()) != map_required)
        return fail("failed to generate source map");
    if (std::strstr(source_map.data(), "frontend.ast_id") == nullptr ||
        std::strstr(source_map.data(), "sample.c") == nullptr)
        return fail("source map omitted frontend metadata");

    const size_t abi_required = forge_module_function_abi_json(module, "add", FORGE_ABI_SYSTEM_V_X86_64, nullptr, 0);
    if (abi_required < 2) return fail("function ABI JSON unavailable");
    std::vector<char> abi_json(abi_required);
    forge_module_function_abi_json(module, "add", FORGE_ABI_SYSTEM_V_X86_64, abi_json.data(), abi_json.size());
    if (std::strstr(abi_json.data(), "integerRegisters") == nullptr)
        return fail("function ABI JSON missing register counts");
    const size_t semantic_required = forge_module_semantic_fingerprint(module, nullptr, 0);
    const size_t frontend_required = forge_module_frontend_fingerprint(module, nullptr, 0);
    if (semantic_required != 65 || frontend_required != 65) return fail("fingerprint size mismatch");
    std::vector<char> semantic(semantic_required);
    std::vector<char> frontend(frontend_required);
    forge_module_semantic_fingerprint(module, semantic.data(), semantic.size());
    forge_module_frontend_fingerprint(module, frontend.data(), frontend.size());
    if (std::strlen(semantic.data()) != 64 || std::strlen(frontend.data()) != 64)
        return fail("fingerprint output mismatch");
    const size_t manifest_required = forge_module_incremental_manifest_json(module, nullptr, 0);
    std::vector<char> manifest(manifest_required);
    forge_module_incremental_manifest_json(module, manifest.data(), manifest.size());
    if (std::strstr(manifest.data(), "semanticFingerprint") == nullptr ||
        std::strstr(manifest.data(), "caller") == nullptr)
        return fail("incremental manifest missing data");
    const size_t cache_required = forge_module_cache_key(module, "c-test", "-O2", nullptr, 0);
    if (cache_required != 65) return fail("cache key size mismatch");
    std::vector<char> cache_key(cache_required);
    forge_module_cache_key(module, "c-test", "-O2", cache_key.data(), cache_key.size());
    if (std::strlen(cache_key.data()) != 64) return fail("cache key output mismatch");

    auto* invalid = forge_function_create(module, "invalid", FORGE_TYPE_I64, nullptr, 0);
    auto* invalid_entry = forge_block_create(invalid, "entry");
    (void)invalid_entry;
    if (forge_module_verify(module, error, sizeof(error)) != 0) return fail("invalid module unexpectedly verified");
    if (forge_module_diagnostic_count(module) == 0) return fail("structured diagnostics missing");
    if (forge_module_diagnostic_severity(module, 0) != FORGE_DIAGNOSTIC_ERROR)
        return fail("diagnostic severity mismatch");
    const char* diagnostic = forge_module_diagnostic_message(module, 0);
    if (diagnostic == nullptr || std::strlen(diagnostic) == 0) return fail("diagnostic message missing");
    (void)forge_module_diagnostic_file(module, 0);
    (void)forge_module_diagnostic_line(module, 0);
    (void)forge_module_diagnostic_column(module, 0);
    (void)forge_module_diagnostic_end_line(module, 0);
    (void)forge_module_diagnostic_end_column(module, 0);
    forge_block_destroy(invalid_entry);
    forge_function_destroy(invalid);

    forge_module_t* current_module = forge_module_create(context, "current");
    if (current_module == nullptr) return fail("failed to create current module");
    forge_function_t* current_function = forge_function_create(current_module, "changed", FORGE_TYPE_VOID, nullptr, 0);
    forge_block_t* current_block = forge_block_create(current_function, "entry");
    forge_builder_t* current_builder = forge_builder_create(context, current_module);
    forge_builder_position_at_end(current_builder, current_block);
    forge_builder_return(current_builder, nullptr);
    const size_t plan_required = forge_module_incremental_build_plan_json(
        module, current_module, "c-test", "-O2;x86_64", nullptr, 0);
    if (plan_required == 0) return fail("incremental build plan unavailable");
    std::vector<char> plan(plan_required);
    forge_module_incremental_build_plan_json(module, current_module, "c-test", "-O2;x86_64", plan.data(), plan.size());
    if (std::string(plan.data()).find("\"rebuild\"") == std::string::npos)
        return fail("incremental build plan missing summary");
    const size_t schedule_required = forge_module_parallel_build_schedule_json(
        module, current_module, "c-test", "-O2;x86_64", 2, nullptr, 0);
    if (schedule_required == 0) return fail("parallel build schedule unavailable");
    std::vector<char> schedule(schedule_required);
    forge_module_parallel_build_schedule_json(
        module, current_module, "c-test", "-O2;x86_64", 2, schedule.data(), schedule.size());
    if (std::string(schedule.data()).find("\"requestedWorkers\":2") == std::string::npos)
        return fail("parallel build schedule missing workers");
    forge_builder_destroy(current_builder);
    forge_block_destroy(current_block);
    forge_function_destroy(current_function);
    forge_module_destroy(current_module);

    const size_t dependency_schedule_required = forge_module_dependency_build_schedule_json(
        module, module, "c", "-O2", 2, nullptr, 0);
    if (dependency_schedule_required == 0) return fail("dependency schedule size failed");
    std::vector<char> dependency_schedule(dependency_schedule_required);
    forge_module_dependency_build_schedule_json(
        module, module, "c", "-O2", 2, dependency_schedule.data(), dependency_schedule.size());
    if (std::strstr(dependency_schedule.data(), "\"levels\"") == nullptr)
        return fail("dependency schedule JSON mismatch");

    forge_builder_destroy(builder);
    forge_block_destroy(caller_entry);
    forge_function_destroy(caller);
    forge_block_destroy(entry);
    forge_function_destroy(add);
    forge_module_destroy(module);

    // C API v14: construct the complete IR model directly without parsing FIR text.
    auto* structured = forge_module_create(context, "structured");
    if (structured == nullptr) return fail("failed to create structured module");
    const size_t pair_index = forge_module_add_struct(structured, "Pair", 0);
    if (pair_index == SIZE_MAX ||
        !forge_struct_add_field(structured, pair_index, "left", FORGE_TYPE_I64, FORGE_AGGREGATE_SCALAR, nullptr) ||
        !forge_struct_add_field(structured, pair_index, "right", FORGE_TYPE_I64, FORGE_AGGREGATE_SCALAR, nullptr))
        return fail("failed to create structured aggregate");
    if (forge_module_add_array(structured, "Words", FORGE_TYPE_I64, 4, 0) == SIZE_MAX)
        return fail("failed to create structured array");
    if (forge_module_add_array_ex(structured, "Pairs", FORGE_TYPE_PTR, FORGE_AGGREGATE_STRUCT, "Pair", 2, 0) == SIZE_MAX)
        return fail("failed to create structured aggregate-element array");
    if (forge_module_add_global(structured, "external_counter", FORGE_TYPE_I64, FORGE_AGGREGATE_SCALAR,
                                nullptr, nullptr, 0, 1, FORGE_LINKAGE_EXTERNAL, FORGE_VISIBILITY_DEFAULT,
                                nullptr, 1, 8, 0, nullptr, 0) == SIZE_MAX)
        return fail("failed to create structured global");
    if (forge_module_add_global_ex(structured, "tls_counter", FORGE_TYPE_I64, FORGE_AGGREGATE_SCALAR,
                                   nullptr, nullptr, 0, 0, 1, FORGE_LINKAGE_INTERNAL, FORGE_VISIBILITY_DEFAULT,
                                   "40", 1, 8, 0, nullptr, 0) == SIZE_MAX)
        return fail("failed to create structured TLS global");
    auto* tls_read = forge_function_create_ex(structured, "tls_read", FORGE_TYPE_I64,
                                                FORGE_AGGREGATE_SCALAR, nullptr, 0, FORGE_BORROW_NONE, -1, 0, 0);
    auto* tls_entry = tls_read ? forge_block_create(tls_read, "entry") : nullptr;
    if (!tls_read || !tls_entry) return fail("failed to create structured TLS reader");
    const size_t tls_address = forge_block_append_operation(tls_entry, "%tls", "tls.address", FORGE_TYPE_PTR);
    const size_t tls_load = forge_block_append_operation(tls_entry, "%tls_value", "load", FORGE_TYPE_I64);
    const size_t tls_return = forge_block_append_operation(tls_entry, nullptr, "return", FORGE_TYPE_VOID);
    if (tls_address == SIZE_MAX || tls_load == SIZE_MAX || tls_return == SIZE_MAX ||
        !forge_operation_add_operand(tls_entry, tls_address, "@tls_counter") ||
        !forge_operation_add_operand(tls_entry, tls_load, "%tls") || !forge_operation_set_alignment(tls_entry, tls_load, 8) ||
        !forge_operation_add_operand(tls_entry, tls_return, "%tls_value"))
        return fail("failed to construct structured TLS operations");

    auto* direct = forge_function_create_ex(structured, "direct_add", FORGE_TYPE_I64,
                                             FORGE_AGGREGATE_SCALAR, nullptr, 0, FORGE_BORROW_NONE, -1, 0, 0);
    if (direct == nullptr ||
        !forge_function_add_parameter(direct, "%left", FORGE_TYPE_I64, FORGE_AGGREGATE_SCALAR, nullptr, 0, FORGE_BORROW_NONE, nullptr) ||
        !forge_function_add_parameter(direct, "%right", FORGE_TYPE_I64, FORGE_AGGREGATE_SCALAR, nullptr, 0, FORGE_BORROW_NONE, nullptr) ||
        !forge_function_set_abi(direct, FORGE_CALL_C, 0, FORGE_LINKAGE_EXTERNAL, FORGE_VISIBILITY_DEFAULT) ||
        !forge_function_set_target_feature(direct, "sse2"))
        return fail("failed to configure structured function");
    auto* direct_entry = forge_block_create(direct, "entry");
    if (direct_entry == nullptr) return fail("failed to create structured block");
    const size_t sum_operation = forge_block_append_operation(direct_entry, "%sum", "add", FORGE_TYPE_I64);
    if (sum_operation == SIZE_MAX ||
        !forge_operation_add_operand(direct_entry, sum_operation, "%left") ||
        !forge_operation_add_operand(direct_entry, sum_operation, "%right") ||
        !forge_operation_set_source_range(direct_entry, sum_operation, "direct.rz", 4, 5, 4, 17) ||
        !forge_operation_add_attribute(direct_entry, sum_operation, "raz.mir", "17"))
        return fail("failed to append structured operation");
    const size_t return_operation = forge_block_append_operation(direct_entry, nullptr, "return", FORGE_TYPE_VOID);
    if (return_operation == SIZE_MAX || !forge_operation_add_operand(direct_entry, return_operation, "%sum"))
        return fail("failed to append structured return");

    auto* choose = forge_function_create_ex(structured, "choose", FORGE_TYPE_I64,
                                             FORGE_AGGREGATE_SCALAR, nullptr, 0, FORGE_BORROW_NONE, -1, 0, 0);
    if (choose == nullptr ||
        !forge_function_add_parameter(choose, "%condition", FORGE_TYPE_I1, FORGE_AGGREGATE_SCALAR, nullptr, 0, FORGE_BORROW_NONE, nullptr) ||
        !forge_function_add_parameter(choose, "%value", FORGE_TYPE_I64, FORGE_AGGREGATE_SCALAR, nullptr, 0, FORGE_BORROW_NONE, nullptr))
        return fail("failed to create structured branch function");
    auto* choose_entry = forge_block_create(choose, "entry");
    auto* choose_true = forge_block_create(choose, "true");
    auto* choose_false = forge_block_create(choose, "false");
    auto* choose_merge = forge_block_create(choose, "merge");
    if (!choose_entry || !choose_true || !choose_false || !choose_merge ||
        !forge_block_add_parameter(choose_merge, "%merged", FORGE_TYPE_I64, FORGE_AGGREGATE_SCALAR, nullptr, 0, FORGE_BORROW_NONE, nullptr))
        return fail("failed to create structured CFG blocks");
    const size_t branch_operation = forge_block_append_operation(choose_entry, nullptr, "branch", FORGE_TYPE_VOID);
    if (branch_operation == SIZE_MAX ||
        !forge_operation_add_operand(choose_entry, branch_operation, "%condition") ||
        !forge_operation_add_successor(choose_entry, branch_operation, "true") ||
        !forge_operation_add_successor(choose_entry, branch_operation, "false"))
        return fail("failed to create structured branch");
    const size_t true_jump = forge_block_append_operation(choose_true, nullptr, "jump", FORGE_TYPE_VOID);
    if (true_jump == SIZE_MAX || !forge_operation_add_successor(choose_true, true_jump, "merge") ||
        !forge_operation_add_successor_argument(choose_true, true_jump, 0, "%value"))
        return fail("failed to create structured true edge");
    const size_t zero_operation = forge_block_append_operation(choose_false, "%zero", "const", FORGE_TYPE_I64);
    if (zero_operation == SIZE_MAX || !forge_operation_add_operand(choose_false, zero_operation, "0"))
        return fail("failed to create structured zero");
    const size_t false_jump = forge_block_append_operation(choose_false, nullptr, "jump", FORGE_TYPE_VOID);
    if (false_jump == SIZE_MAX || !forge_operation_add_successor(choose_false, false_jump, "merge") ||
        !forge_operation_add_successor_argument(choose_false, false_jump, 0, "%zero"))
        return fail("failed to create structured false edge");
    const size_t merge_return = forge_block_append_operation(choose_merge, nullptr, "return", FORGE_TYPE_VOID);
    if (merge_return == SIZE_MAX || !forge_operation_add_operand(choose_merge, merge_return, "%merged"))
        return fail("failed to create structured merge return");

    // Structured aggregate ABI regression: C API aggregate metadata must survive
    // parameter definition, generic copy, aggregate return, verification, and
    // native object emission without passing through the text parser.
    auto* aggregate_identity = forge_function_create_ex(structured, "aggregate_identity", FORGE_TYPE_PTR,
                                                          FORGE_AGGREGATE_STRUCT, "Pair", 1, FORGE_BORROW_NONE, -1, 0, 0);
    auto* aggregate_entry = aggregate_identity ? forge_block_create(aggregate_identity, "entry") : nullptr;
    if (aggregate_identity == nullptr || aggregate_entry == nullptr ||
        !forge_function_add_parameter(aggregate_identity, "%pair", FORGE_TYPE_PTR, FORGE_AGGREGATE_STRUCT, "Pair", 1, FORGE_BORROW_NONE, nullptr))
        return fail("failed to configure structured aggregate ABI function");
    const size_t aggregate_copy = forge_block_append_operation(aggregate_entry, "%copy", "copy", FORGE_TYPE_PTR);
    const size_t aggregate_return = forge_block_append_operation(aggregate_entry, nullptr, "return", FORGE_TYPE_VOID);
    if (aggregate_copy == SIZE_MAX || aggregate_return == SIZE_MAX ||
        !forge_operation_add_operand(aggregate_entry, aggregate_copy, "%pair") ||
        !forge_operation_add_operand(aggregate_entry, aggregate_return, "%copy"))
        return fail("failed to construct structured aggregate ABI operations");

    // Structured fixed-array ABI regression: array identity must survive
    // parameter definition, generic copy, aggregate return, verification, and
    // native object emission without textual FIR.
    auto* array_identity = forge_function_create_ex(structured, "array_identity", FORGE_TYPE_PTR,
                                                      FORGE_AGGREGATE_ARRAY, "Words", 1, FORGE_BORROW_NONE, -1, 0, 0);
    auto* array_entry = array_identity ? forge_block_create(array_identity, "entry") : nullptr;
    if (array_identity == nullptr || array_entry == nullptr ||
        !forge_function_add_parameter(array_identity, "%words", FORGE_TYPE_PTR, FORGE_AGGREGATE_ARRAY, "Words", 1, FORGE_BORROW_NONE, nullptr))
        return fail("failed to configure structured array ABI function");
    const size_t array_copy = forge_block_append_operation(array_entry, "%copy", "copy", FORGE_TYPE_PTR);
    const size_t array_return = forge_block_append_operation(array_entry, nullptr, "return", FORGE_TYPE_VOID);
    if (array_copy == SIZE_MAX || array_return == SIZE_MAX ||
        !forge_operation_add_operand(array_entry, array_copy, "%words") ||
        !forge_operation_add_operand(array_entry, array_return, "%copy"))
        return fail("failed to construct structured array ABI operations");

    // Dynamic aggregate-element array addressing: the index may be an SSA i64
    // register and machine lowering must scale it by the named array's actual
    // aggregate stride.
    auto* dynamic_pair = forge_function_create_ex(structured, "dynamic_pair_left", FORGE_TYPE_I64,
                                                    FORGE_AGGREGATE_SCALAR, nullptr, 0, FORGE_BORROW_NONE, -1, 0, 0);
    auto* dynamic_pair_entry = dynamic_pair ? forge_block_create(dynamic_pair, "entry") : nullptr;
    if (dynamic_pair == nullptr || dynamic_pair_entry == nullptr ||
        !forge_function_add_parameter(dynamic_pair, "%pairs", FORGE_TYPE_PTR, FORGE_AGGREGATE_ARRAY, "Pairs", 1, FORGE_BORROW_NONE, nullptr) ||
        !forge_function_add_parameter(dynamic_pair, "%index", FORGE_TYPE_I64, FORGE_AGGREGATE_SCALAR, nullptr, 0, FORGE_BORROW_NONE, nullptr))
        return fail("failed to configure dynamic aggregate-array function");
    const size_t pair_element = forge_block_append_operation(dynamic_pair_entry, "%element", "array.element.address", FORGE_TYPE_PTR);
    const size_t pair_field = forge_block_append_operation(dynamic_pair_entry, "%field", "struct.field.address", FORGE_TYPE_PTR);
    const size_t pair_load = forge_block_append_operation(dynamic_pair_entry, "%value", "load", FORGE_TYPE_I64);
    const size_t pair_return = forge_block_append_operation(dynamic_pair_entry, nullptr, "return", FORGE_TYPE_VOID);
    if (pair_element == SIZE_MAX || pair_field == SIZE_MAX || pair_load == SIZE_MAX || pair_return == SIZE_MAX ||
        !forge_operation_add_operand(dynamic_pair_entry, pair_element, "%pairs") ||
        !forge_operation_add_operand(dynamic_pair_entry, pair_element, "@Pairs") ||
        !forge_operation_add_operand(dynamic_pair_entry, pair_element, "%index") ||
        !forge_operation_add_operand(dynamic_pair_entry, pair_field, "%element") ||
        !forge_operation_add_operand(dynamic_pair_entry, pair_field, "@Pair") ||
        !forge_operation_add_operand(dynamic_pair_entry, pair_field, "0") ||
        !forge_operation_add_operand(dynamic_pair_entry, pair_load, "%field") ||
        !forge_operation_set_alignment(dynamic_pair_entry, pair_load, 8) ||
        !forge_operation_add_operand(dynamic_pair_entry, pair_return, "%value"))
        return fail("failed to construct dynamic aggregate-array operations");

    // Structured slice ABI regression: a Raz slice is the native aggregate
    // {data pointer, length}. Preserve that aggregate identity through copy and
    // return so frontends can pass/return slices without textual FIR.
    const size_t slice_index = forge_module_add_struct(structured, "__raz_slice", 0);
    if (slice_index == SIZE_MAX ||
        !forge_struct_add_field(structured, slice_index, "data", FORGE_TYPE_PTR, FORGE_AGGREGATE_SCALAR, nullptr) ||
        !forge_struct_add_field(structured, slice_index, "length", FORGE_TYPE_I64, FORGE_AGGREGATE_SCALAR, nullptr))
        return fail("failed to create structured slice aggregate");
    auto* slice_identity = forge_function_create_ex(structured, "slice_identity", FORGE_TYPE_PTR,
                                                      FORGE_AGGREGATE_STRUCT, "__raz_slice", 1, FORGE_BORROW_NONE, -1, 0, 0);
    auto* slice_entry = slice_identity ? forge_block_create(slice_identity, "entry") : nullptr;
    if (slice_identity == nullptr || slice_entry == nullptr ||
        !forge_function_add_parameter(slice_identity, "%slice", FORGE_TYPE_PTR, FORGE_AGGREGATE_STRUCT, "__raz_slice", 1, FORGE_BORROW_NONE, nullptr))
        return fail("failed to configure structured slice ABI function");
    const size_t slice_copy = forge_block_append_operation(slice_entry, "%copy", "copy", FORGE_TYPE_PTR);
    const size_t slice_return = forge_block_append_operation(slice_entry, nullptr, "return", FORGE_TYPE_VOID);
    if (slice_copy == SIZE_MAX || slice_return == SIZE_MAX ||
        !forge_operation_add_operand(slice_entry, slice_copy, "%slice") ||
        !forge_operation_add_operand(slice_entry, slice_return, "%copy"))
        return fail("failed to construct structured slice ABI operations");

    // Machine-lowering regression: a stack address materialized as the first
    // virtual register is register 0, which is valid and must not be confused
    // with an "unset" sentinel when storing a pointer.
    auto* pointer_slot = forge_function_create_ex(structured, "pointer_slot", FORGE_TYPE_I64,
                                                   FORGE_AGGREGATE_SCALAR, nullptr, 0, FORGE_BORROW_NONE, -1, 0, 0);
    auto* pointer_entry = pointer_slot ? forge_block_create(pointer_slot, "entry") : nullptr;
    if (pointer_slot == nullptr || pointer_entry == nullptr) return fail("failed to create pointer-slot regression function");
    const size_t holder = forge_block_append_operation(pointer_entry, "%holder", "stack.alloc", FORGE_TYPE_PTR);
    const size_t payload = forge_block_append_operation(pointer_entry, "%payload", "stack.alloc", FORGE_TYPE_PTR);
    const size_t pointer_store = forge_block_append_operation(pointer_entry, nullptr, "store", FORGE_TYPE_PTR);
    const size_t pointer_load = forge_block_append_operation(pointer_entry, "%loaded", "load", FORGE_TYPE_PTR);
    const size_t pointer_value = forge_block_append_operation(pointer_entry, "%value", "const", FORGE_TYPE_I64);
    const size_t value_store = forge_block_append_operation(pointer_entry, nullptr, "store", FORGE_TYPE_I64);
    const size_t value_load = forge_block_append_operation(pointer_entry, "%result", "load", FORGE_TYPE_I64);
    const size_t pointer_return = forge_block_append_operation(pointer_entry, nullptr, "return", FORGE_TYPE_VOID);
    if (holder == SIZE_MAX || payload == SIZE_MAX || pointer_store == SIZE_MAX || pointer_load == SIZE_MAX ||
        pointer_value == SIZE_MAX || value_store == SIZE_MAX || value_load == SIZE_MAX || pointer_return == SIZE_MAX ||
        !forge_operation_add_operand(pointer_entry, holder, "8") || !forge_operation_set_alignment(pointer_entry, holder, 8) ||
        !forge_operation_add_operand(pointer_entry, payload, "8") || !forge_operation_set_alignment(pointer_entry, payload, 8) ||
        !forge_operation_add_operand(pointer_entry, pointer_store, "%payload") || !forge_operation_add_operand(pointer_entry, pointer_store, "%holder") || !forge_operation_set_alignment(pointer_entry, pointer_store, 8) ||
        !forge_operation_add_operand(pointer_entry, pointer_load, "%holder") || !forge_operation_set_alignment(pointer_entry, pointer_load, 8) ||
        !forge_operation_add_operand(pointer_entry, pointer_value, "42") ||
        !forge_operation_add_operand(pointer_entry, value_store, "%value") || !forge_operation_add_operand(pointer_entry, value_store, "%loaded") || !forge_operation_set_alignment(pointer_entry, value_store, 8) ||
        !forge_operation_add_operand(pointer_entry, value_load, "%payload") || !forge_operation_set_alignment(pointer_entry, value_load, 8) ||
        !forge_operation_add_operand(pointer_entry, pointer_return, "%result"))
        return fail("failed to construct pointer-slot regression function");

    if (forge_module_verify(structured, error, sizeof(error)) != 1) {
        std::cerr << "structured verification failed: " << error << '\n';
        return 1;
    }
    const size_t structured_required = forge_module_print(structured, nullptr, 0);
    std::vector<char> structured_text(structured_required);
    forge_module_print(structured, structured_text.data(), structured_text.size());
    if (std::strstr(structured_text.data(), "%sum = add i64 %left %right") == nullptr ||
        std::strstr(structured_text.data(), "merge(%merged: i64)") == nullptr ||
        std::strstr(structured_text.data(), "struct @Pair") == nullptr ||
        std::strstr(structured_text.data(), "thread_local internal global @tls_counter") == nullptr ||
        std::strstr(structured_text.data(), "tls.address ptr @tls_counter") == nullptr)
        return fail("structured module print mismatch");
    if (!forge_module_optimize(structured, FORGE_OPT_O2)) return fail("failed to optimize structured module");
#if defined(_WIN32)
    constexpr forge_native_abi_t structured_abi = FORGE_ABI_WINDOWS_X64;
#else
    constexpr forge_native_abi_t structured_abi = FORGE_ABI_SYSTEM_V_X86_64;
#endif
    const size_t structured_object_size = forge_module_emit_object(structured, structured_abi, nullptr, 0);
    if (structured_object_size == 0) return fail("failed to emit structured module object");
    std::vector<uint8_t> structured_object(structured_object_size);
    if (forge_module_emit_object(structured, structured_abi, structured_object.data(), structured_object.size()) != structured_object_size)
        return fail("failed to materialize structured module object");

    forge_block_destroy(tls_entry);
    forge_function_destroy(tls_read);
    forge_block_destroy(slice_entry);
    forge_function_destroy(slice_identity);
    forge_block_destroy(array_entry);
    forge_function_destroy(array_identity);
    forge_function_destroy(dynamic_pair);
    forge_block_destroy(aggregate_entry);
    forge_function_destroy(aggregate_identity);
    forge_block_destroy(pointer_entry);
    forge_function_destroy(pointer_slot);
    forge_block_destroy(choose_merge);
    forge_block_destroy(choose_false);
    forge_block_destroy(choose_true);
    forge_block_destroy(choose_entry);
    forge_function_destroy(choose);
    forge_block_destroy(direct_entry);
    forge_function_destroy(direct);
    forge_module_destroy(structured);

    const char parsed_source[] =
        "module @parsed {\n"
        "  func @answer() -> i64 {\n"
        "  entry:\n"
        "    %value = const i64 42\n"
        "    return %value\n"
        "  }\n"
        "}\n";
    auto* parsed_module = forge_module_parse(context, parsed_source, std::strlen(parsed_source));
    if (parsed_module == nullptr) return fail("failed to parse module through C API");
    if (!forge_module_optimize(parsed_module, FORGE_OPT_O2)) return fail("failed to optimize parsed module");
#if defined(_WIN32)
    constexpr forge_native_abi_t host_abi = FORGE_ABI_WINDOWS_X64;
#else
    constexpr forge_native_abi_t host_abi = FORGE_ABI_SYSTEM_V_X86_64;
#endif
    const size_t object_required = forge_module_emit_object(parsed_module, host_abi, nullptr, 0);
    if (object_required == 0) return fail("failed to size native object");
    std::vector<uint8_t> object(object_required);
    if (forge_module_emit_object(parsed_module, host_abi, object.data(), object.size()) != object_required)
        return fail("failed to emit native object");
    if (object.size() < 32) return fail("native object unexpectedly small");
    forge_module_destroy(parsed_module);

    forge_context_destroy(context);
    std::cout << "C frontend API v14 ABI test passed\n";
    return 0;
}
