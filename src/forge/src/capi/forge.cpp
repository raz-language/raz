// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge-c/forge.h"
#include "forge/ir/builder.hpp"
#include "forge/ir/incremental.hpp"
#include "forge/ir/artifact_cache.hpp"
#include "forge/ir/build_driver.hpp"
#include "forge/ir/dependency_build.hpp"
#include "forge/ir/parser.hpp"
#include "forge/ir/printer.hpp"
#include "forge/ir/source_map.hpp"
#include "forge/ir/verifier.hpp"
#include "forge/diagnostics/format.hpp"
#include "forge/machine/lower.hpp"
#include "forge/object/coff.hpp"
#include "forge/object/elf.hpp"
#include "forge/pass/pipeline.hpp"
#include "forge/target/abi.hpp"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <sstream>
#include <vector>

struct forge_context { forge::ir::Context value; };
struct forge_module {
    forge::ir::Module* value{};
    std::unique_ptr<forge::ir::Module> owned;
    mutable forge::Diagnostics diagnostics;
    mutable std::string scratch;
};
struct forge_function { forge_module* module{}; size_t index{}; std::vector<std::string> parameter_names; };
struct forge_block { forge_function* function{}; size_t index{}; std::vector<std::string> parameter_names; };
struct forge_builder { std::unique_ptr<forge::ir::IRBuilder> value; std::string result; };

namespace {
thread_local std::string last_error;
void set_error(const char* message) { last_error = message ? message : "unknown Forge C API error"; }
forge::ir::Type type_of(forge_type_kind_t kind) {
    using forge::ir::TypeKind;
    switch (kind) {
    case FORGE_TYPE_VOID: return forge::ir::Type(TypeKind::void_);
    case FORGE_TYPE_I1: return forge::ir::Type(TypeKind::i1);
    case FORGE_TYPE_I8: return forge::ir::Type(TypeKind::i8);
    case FORGE_TYPE_I16: return forge::ir::Type(TypeKind::i16);
    case FORGE_TYPE_I32: return forge::ir::Type(TypeKind::i32);
    case FORGE_TYPE_I64: return forge::ir::Type(TypeKind::i64);
    case FORGE_TYPE_F32: return forge::ir::Type(TypeKind::f32);
    case FORGE_TYPE_F64: return forge::ir::Type(TypeKind::f64);
    case FORGE_TYPE_PTR: return forge::ir::Type(TypeKind::ptr);
    }

    return forge::ir::Type(TypeKind::void_);
}

forge::ir::AggregateRefKind aggregate_kind_of(forge_aggregate_ref_kind_t value) {
    switch (value) {
    case FORGE_AGGREGATE_SCALAR: return forge::ir::AggregateRefKind::scalar;
    case FORGE_AGGREGATE_STRUCT: return forge::ir::AggregateRefKind::structure;
    case FORGE_AGGREGATE_ARRAY: return forge::ir::AggregateRefKind::array;
    }
    return forge::ir::AggregateRefKind::scalar;
}

forge::ir::BorrowMode borrow_mode_of(forge_borrow_mode_t value) {
    switch (value) {
    case FORGE_BORROW_NONE: return forge::ir::BorrowMode::none;
    case FORGE_BORROW_IMMUTABLE: return forge::ir::BorrowMode::immutable;
    case FORGE_BORROW_MUTABLE: return forge::ir::BorrowMode::mutable_;
    }
    return forge::ir::BorrowMode::none;
}

forge::ir::ValueDecl value_decl_of(const char* name, forge_type_kind_t type,
                                   forge_aggregate_ref_kind_t aggregate_kind,
                                   const char* aggregate_name, int owned,
                                   forge_borrow_mode_t borrow_mode,
                                   const char* function_signature_name) {
    forge::ir::ValueDecl value{name ? name : "", type_of(type)};
    value.aggregate_kind = aggregate_kind_of(aggregate_kind);
    value.aggregate_name = aggregate_name ? aggregate_name : "";
    value.owned = owned != 0;
    value.borrow_mode = borrow_mode_of(borrow_mode);
    value.function_signature_name = function_signature_name ? function_signature_name : "";
    return value;
}

forge::ir::CallingConvention calling_convention_of(forge_calling_convention_t value) {
    switch (value) {
    case FORGE_CALL_PLATFORM: return forge::ir::CallingConvention::platform;
    case FORGE_CALL_C: return forge::ir::CallingConvention::c;
    case FORGE_CALL_SYSTEM_V: return forge::ir::CallingConvention::system_v;
    case FORGE_CALL_WINDOWS_X64: return forge::ir::CallingConvention::windows_x64;
    case FORGE_CALL_FAST: return forge::ir::CallingConvention::fast;
    }
    return forge::ir::CallingConvention::platform;
}

forge::ir::SymbolLinkage linkage_of(forge_symbol_linkage_t value) {
    switch (value) {
    case FORGE_LINKAGE_EXTERNAL: return forge::ir::SymbolLinkage::external;
    case FORGE_LINKAGE_INTERNAL: return forge::ir::SymbolLinkage::internal;
    case FORGE_LINKAGE_WEAK: return forge::ir::SymbolLinkage::weak;
    }
    return forge::ir::SymbolLinkage::external;
}

forge::ir::SymbolVisibility visibility_of(forge_symbol_visibility_t value) {
    return value == FORGE_VISIBILITY_HIDDEN ? forge::ir::SymbolVisibility::hidden
                                            : forge::ir::SymbolVisibility::default_;
}

forge::pass::OptimizationLevel optimization_level_of(forge_optimization_level_t value) {
    using forge::pass::OptimizationLevel;
    switch (value) {
    case FORGE_OPT_O0: return OptimizationLevel::o0;
    case FORGE_OPT_O1: return OptimizationLevel::o1;
    case FORGE_OPT_O2: return OptimizationLevel::o2;
    case FORGE_OPT_O3: return OptimizationLevel::o3;
    case FORGE_OPT_OS: return OptimizationLevel::os;
    case FORGE_OPT_OZ: return OptimizationLevel::oz;
    }
    return OptimizationLevel::o2;
}

size_t copy_text(std::string_view text, char* output, size_t capacity) {
    const size_t required = text.size() + 1;
    if (output && capacity) {
        const auto count = std::min(capacity - 1, text.size());
        std::memcpy(output, text.data(), count);
        output[count] = '\0';
    }
    return required;
}

forge::ir::Opcode opcode_of(forge_opcode_t opcode) {
    using forge::ir::Opcode;
    switch (opcode) {
    case FORGE_OPCODE_ADD: return Opcode::add;
    case FORGE_OPCODE_SUBTRACT: return Opcode::subtract;
    case FORGE_OPCODE_MULTIPLY: return Opcode::multiply;
    case FORGE_OPCODE_DIVIDE_SIGNED: return Opcode::divide_signed;
    case FORGE_OPCODE_DIVIDE_UNSIGNED: return Opcode::divide_unsigned;
    case FORGE_OPCODE_REMAINDER_SIGNED: return Opcode::remainder_signed;
    case FORGE_OPCODE_REMAINDER_UNSIGNED: return Opcode::remainder_unsigned;
    case FORGE_OPCODE_AND: return Opcode::bit_and;
    case FORGE_OPCODE_OR: return Opcode::bit_or;
    case FORGE_OPCODE_XOR: return Opcode::bit_xor;
    case FORGE_OPCODE_SHIFT_LEFT: return Opcode::shift_left;
    case FORGE_OPCODE_SHIFT_RIGHT_SIGNED: return Opcode::shift_right_signed;
    case FORGE_OPCODE_SHIFT_RIGHT_UNSIGNED: return Opcode::shift_right_unsigned;
    case FORGE_OPCODE_COMPARE_EQUAL: return Opcode::compare_equal;
    case FORGE_OPCODE_COMPARE_NOT_EQUAL: return Opcode::compare_not_equal;
    case FORGE_OPCODE_COMPARE_LESS_SIGNED: return Opcode::compare_less_signed;
    case FORGE_OPCODE_COMPARE_LESS_UNSIGNED: return Opcode::compare_less_unsigned;
    case FORGE_OPCODE_COMPARE_LESS_EQUAL_SIGNED: return Opcode::compare_less_equal_signed;
    case FORGE_OPCODE_COMPARE_LESS_EQUAL_UNSIGNED: return Opcode::compare_less_equal_unsigned;
    }
    return Opcode::add;
}

forge::ir::Function* resolve(forge_function* handle) {
    if (handle == nullptr || handle->module == nullptr || handle->module->value == nullptr ||
        handle->index >= handle->module->value->functions().size()) return nullptr;
    return &handle->module->value->functions()[handle->index];
}

forge::ir::Block* resolve(forge_block* handle) {
    auto* function = handle ? resolve(handle->function) : nullptr;
    if (function == nullptr || handle->index >= function->blocks.size()) return nullptr;
    return &function->blocks[handle->index];
}

const forge::ir::Block* resolve(const forge_block* handle) { return resolve(const_cast<forge_block*>(handle)); }
template <typename F> const char* result_call(forge_builder* builder, F&& action) {
    if (builder == nullptr || builder->value == nullptr) { set_error("builder is null"); return nullptr; }
    try { builder->result = action(); last_error.clear(); return builder->result.c_str(); }
    catch (const std::exception& error) { last_error = error.what(); return nullptr; }
}

std::vector<std::string> string_args(const char* const* arguments, size_t count) {
    std::vector<std::string> result;
    result.reserve(count);
    for (size_t i = 0; i < count; ++i) result.emplace_back(arguments && arguments[i] ? arguments[i] : "");
    return result;
}
}

extern "C" {
forge_context_t* forge_context_create(void) { try { last_error.clear(); return new forge_context; } catch (...) { set_error("failed to create Forge context"); return nullptr; } }
void forge_context_destroy(forge_context_t* context) { delete context; }
forge_module_t* forge_module_create(forge_context_t* context, const char* name) {
    if (context == nullptr) { set_error("context is null"); return nullptr; }
    try {
        auto* module = new forge_module;
        module->value = &context->value.create_module(name ? name : "anonymous");
        last_error.clear();
        return module;
    }

    catch (...) { set_error("failed to create module"); return nullptr; }
}

forge_module_t* forge_module_parse(forge_context_t* context, const char* source, size_t source_length) {
    if (context == nullptr) { set_error("context is null"); return nullptr; }
    if (source == nullptr && source_length != 0) { set_error("source is null"); return nullptr; }
    try {
        const std::string_view text(source ? source : "", source_length);
        auto parsed = forge::ir::parse_module(text);
        if (!parsed.ok()) {
            last_error = forge::diagnostics::render_all(parsed.diagnostics);
            if (last_error.empty()) set_error("failed to parse Forge IR");
            return nullptr;
        }
        auto* module = new forge_module;
        module->owned = std::make_unique<forge::ir::Module>(std::move(*parsed.module));
        module->value = module->owned.get();
        last_error.clear();
        return module;
    } catch (const std::exception& error) {
        last_error = error.what();
        return nullptr;
    } catch (...) {
        set_error("failed to parse Forge IR");
        return nullptr;
    }
}

void forge_module_destroy(forge_module_t* module) { delete module; }

size_t forge_module_add_struct(forge_module_t* module, const char* name, int move_only) {
    if (!module || !module->value) { set_error("module is null"); return SIZE_MAX; }
    try {
        forge::ir::StructDecl declaration;
        declaration.name = name ? name : "";
        declaration.move_only = move_only != 0;
        module->value->structs().push_back(std::move(declaration));
        last_error.clear();
        return module->value->structs().size() - 1;
    } catch (const std::exception& error) { last_error = error.what(); return SIZE_MAX; }
}

int forge_struct_add_field(forge_module_t* module, size_t struct_index,
                           const char* name, forge_type_kind_t type,
                           forge_aggregate_ref_kind_t aggregate_kind,
                           const char* aggregate_name) {
    if (!module || !module->value || struct_index >= module->value->structs().size()) {
        set_error("structure is invalid"); return 0;
    }
    try {
        forge::ir::StructField field{name ? name : "", type_of(type),
                                    aggregate_kind_of(aggregate_kind), aggregate_name ? aggregate_name : ""};
        module->value->structs()[struct_index].fields.push_back(std::move(field));
        last_error.clear();
        return 1;
    } catch (const std::exception& error) { last_error = error.what(); return 0; }
}

size_t forge_module_add_array_ex(forge_module_t* module, const char* name,
                                 forge_type_kind_t element_type,
                                 forge_aggregate_ref_kind_t element_aggregate_kind,
                                 const char* element_aggregate_name,
                                 uint32_t element_count, int move_only) {
    if (!module || !module->value) { set_error("module is null"); return SIZE_MAX; }
    try {
        forge::ir::ArrayDecl declaration;
        declaration.name = name ? name : "";
        declaration.element_type = type_of(element_type);
        declaration.element_aggregate_kind = aggregate_kind_of(element_aggregate_kind);
        declaration.element_aggregate_name = element_aggregate_name ? element_aggregate_name : "";
        declaration.element_count = element_count;
        declaration.move_only = move_only != 0;
        module->value->arrays().push_back(std::move(declaration));
        last_error.clear();
        return module->value->arrays().size() - 1;
    } catch (const std::exception& error) { last_error = error.what(); return SIZE_MAX; }
}

size_t forge_module_add_array(forge_module_t* module, const char* name,
                              forge_type_kind_t element_type, uint32_t element_count,
                              int move_only) {
    return forge_module_add_array_ex(module, name, element_type, FORGE_AGGREGATE_SCALAR, nullptr,
                                     element_count, move_only);
}

size_t forge_module_add_global_ex(forge_module_t* module, const char* name, forge_type_kind_t type,
                               forge_aggregate_ref_kind_t aggregate_kind, const char* aggregate_name,
                               const char* function_signature_name, int is_constant, int is_external, int is_thread_local,
                               forge_symbol_linkage_t linkage, forge_symbol_visibility_t visibility,
                               const char* initializer, uint32_t element_count, uint32_t alignment,
                               int zero_initialized, const uint8_t* bytes, size_t byte_count) {
    if (!module || !module->value) { set_error("module is null"); return SIZE_MAX; }
    if (byte_count != 0 && bytes == nullptr) { set_error("global bytes are null"); return SIZE_MAX; }
    try {
        forge::ir::Global global;
        global.name = name ? name : "";
        global.type = type_of(type);
        global.aggregate_kind = aggregate_kind_of(aggregate_kind);
        global.aggregate_name = aggregate_name ? aggregate_name : "";
        global.function_signature_name = function_signature_name ? function_signature_name : "";
        global.is_constant = is_constant != 0;
        global.is_external = is_external != 0;
        global.is_thread_local = is_thread_local != 0;
        global.linkage = linkage_of(linkage);
        global.visibility = visibility_of(visibility);
        global.initializer = initializer ? initializer : "";
        global.element_count = element_count == 0 ? 1u : element_count;
        global.alignment = alignment;
        global.zero_initialized = zero_initialized != 0;
        if (byte_count != 0) global.bytes.assign(bytes, bytes + byte_count);
        module->value->globals().push_back(std::move(global));
        last_error.clear();
        return module->value->globals().size() - 1;
    } catch (const std::exception& error) { last_error = error.what(); return SIZE_MAX; }
}

size_t forge_module_add_global(forge_module_t* module, const char* name, forge_type_kind_t type,
                               forge_aggregate_ref_kind_t aggregate_kind, const char* aggregate_name,
                               const char* function_signature_name, int is_constant, int is_external,
                               forge_symbol_linkage_t linkage, forge_symbol_visibility_t visibility,
                               const char* initializer, uint32_t element_count, uint32_t alignment,
                               int zero_initialized, const uint8_t* bytes, size_t byte_count) {
    return forge_module_add_global_ex(module, name, type, aggregate_kind, aggregate_name,
                                      function_signature_name, is_constant, is_external, 0, linkage, visibility,
                                      initializer, element_count, alignment, zero_initialized, bytes, byte_count);
}

forge_function_t* forge_function_create_ex(
    forge_module_t* module, const char* name, forge_type_kind_t return_type,
    forge_aggregate_ref_kind_t return_aggregate_kind, const char* return_aggregate_name,
    int return_owned, forge_borrow_mode_t return_borrow_mode, int32_t return_borrow_parameter,
    int is_external, int is_signature) {
    if (!module || !module->value) { set_error("module is null"); return nullptr; }
    try {
        forge::ir::Function function;
        function.name = name ? name : "anonymous";
        function.return_type = type_of(return_type);
        function.return_aggregate_kind = aggregate_kind_of(return_aggregate_kind);
        function.return_aggregate_name = return_aggregate_name ? return_aggregate_name : "";
        function.return_owned = return_owned != 0;
        function.return_borrow_mode = borrow_mode_of(return_borrow_mode);
        function.return_borrow_parameter = return_borrow_parameter;
        function.is_external = is_external != 0;
        function.is_signature = is_signature != 0;
        auto handle = std::make_unique<forge_function>();
        handle->module = module;
        handle->index = module->value->functions().size();
        module->value->functions().push_back(std::move(function));
        last_error.clear();
        return handle.release();
    } catch (const std::exception& error) { last_error = error.what(); return nullptr; }
}

int forge_function_add_parameter(forge_function_t* function, const char* name, forge_type_kind_t type,
                                 forge_aggregate_ref_kind_t aggregate_kind, const char* aggregate_name,
                                 int owned, forge_borrow_mode_t borrow_mode,
                                 const char* function_signature_name) {
    auto* resolved = resolve(function);
    if (!resolved) { set_error("function is invalid"); return 0; }
    try {
        auto parameter = value_decl_of(name, type, aggregate_kind, aggregate_name, owned,
                                       borrow_mode, function_signature_name);
        function->parameter_names.push_back(parameter.name);
        resolved->parameters.push_back(std::move(parameter));
        last_error.clear();
        return 1;
    } catch (const std::exception& error) { last_error = error.what(); return 0; }
}

int forge_function_set_target_feature(forge_function_t* function, const char* target_feature) {
    auto* resolved = resolve(function);
    if (!resolved) { set_error("function is invalid"); return 0; }
    resolved->target_feature = target_feature ? target_feature : "";
    last_error.clear();
    return 1;
}

forge_function_t* forge_function_create(forge_module_t* module, const char* name, forge_type_kind_t return_type,
                                        const forge_type_kind_t* parameter_types, size_t parameter_count) {
    if (module == nullptr || module->value == nullptr) { set_error("module is null"); return nullptr; }
    if (parameter_count != 0 && parameter_types == nullptr) { set_error("parameter types are null"); return nullptr; }
    try {
        forge::ir::Function fn; fn.name = name ? name : "anonymous"; fn.return_type = type_of(return_type);
        auto handle = std::make_unique<forge_function>(); handle->module = module; handle->index = module->value->functions().size();
        for (size_t i = 0; i < parameter_count; ++i) {
            auto parameter_name = "%arg" + std::to_string(i);
            fn.parameters.emplace_back(parameter_name, type_of(parameter_types[i]));
            handle->parameter_names.push_back(std::move(parameter_name));
        }
        module->value->functions().push_back(std::move(fn)); last_error.clear(); return handle.release();
    } catch (...) { set_error("failed to create function"); return nullptr; }
}

void forge_function_destroy(forge_function_t* function) { delete function; }
int forge_function_set_abi(forge_function_t* function,
                           forge_calling_convention_t calling_convention,
                           int variadic,
                           forge_symbol_linkage_t linkage,
                           forge_symbol_visibility_t visibility) {
    auto* resolved = resolve(function);
    if (!resolved) { set_error("function is invalid"); return 0; }
    resolved->calling_convention = calling_convention_of(calling_convention);
    resolved->variadic = variadic != 0;
    resolved->linkage = linkage_of(linkage);
    resolved->visibility = visibility_of(visibility);
    last_error.clear();
    return 1;
}

forge_block_t* forge_block_create_with_parameters(forge_function_t* function, const char* name,
                                                   const forge_type_kind_t* parameter_types, size_t parameter_count) {
    auto* fn = resolve(function);
    if (fn == nullptr) { set_error("function is invalid"); return nullptr; }
    if (parameter_count != 0 && parameter_types == nullptr) { set_error("block parameter types are null"); return nullptr; }
    try {
        forge::ir::Block block; block.name = name ? name : "entry";
        auto handle = std::make_unique<forge_block>(); handle->function = function; handle->index = fn->blocks.size();
        for (size_t i = 0; i < parameter_count; ++i) {
            auto parameter_name = "%block_arg" + std::to_string(i);
            block.parameters.emplace_back(parameter_name, type_of(parameter_types[i]));
            handle->parameter_names.push_back(std::move(parameter_name));
        }
        fn->blocks.push_back(std::move(block)); last_error.clear(); return handle.release();
    } catch (...) { set_error("failed to create block"); return nullptr; }
}

forge_block_t* forge_block_create(forge_function_t* function, const char* name) { return forge_block_create_with_parameters(function, name, nullptr, 0); }

int forge_block_add_parameter(forge_block_t* block, const char* name, forge_type_kind_t type,
                              forge_aggregate_ref_kind_t aggregate_kind, const char* aggregate_name,
                              int owned, forge_borrow_mode_t borrow_mode,
                              const char* function_signature_name) {
    auto* resolved = resolve(block);
    if (!resolved) { set_error("block is invalid"); return 0; }
    try {
        auto parameter = value_decl_of(name, type, aggregate_kind, aggregate_name, owned,
                                       borrow_mode, function_signature_name);
        block->parameter_names.push_back(parameter.name);
        resolved->parameters.push_back(std::move(parameter));
        last_error.clear();
        return 1;
    } catch (const std::exception& error) { last_error = error.what(); return 0; }
}

size_t forge_block_append_operation(forge_block_t* block, const char* result,
                                    const char* opcode, forge_type_kind_t type) {
    auto* resolved = resolve(block);
    if (!resolved) { set_error("block is invalid"); return SIZE_MAX; }
    if (!opcode || *opcode == '\0') { set_error("operation opcode is empty"); return SIZE_MAX; }
    try {
        forge::ir::Operation operation;
        operation.result = result ? result : "";
        operation.opcode = opcode;
        operation.type = type_of(type);
        resolved->operations.push_back(std::move(operation));
        last_error.clear();
        return resolved->operations.size() - 1;
    } catch (const std::exception& error) { last_error = error.what(); return SIZE_MAX; }
}

int forge_operation_add_operand(forge_block_t* block, size_t operation_index, const char* operand) {
    auto* resolved = resolve(block);
    if (!resolved || operation_index >= resolved->operations.size()) { set_error("operation is invalid"); return 0; }
    try { resolved->operations[operation_index].operands.emplace_back(operand ? operand : ""); last_error.clear(); return 1; }
    catch (const std::exception& error) { last_error = error.what(); return 0; }
}

int forge_operation_add_successor(forge_block_t* block, size_t operation_index, const char* successor) {
    auto* resolved = resolve(block);
    if (!resolved || operation_index >= resolved->operations.size()) { set_error("operation is invalid"); return 0; }
    try {
        auto& operation = resolved->operations[operation_index];
        operation.successors.emplace_back(successor ? successor : "");
        operation.successor_arguments.emplace_back();
        last_error.clear();
        return 1;
    } catch (const std::exception& error) { last_error = error.what(); return 0; }
}

int forge_operation_add_successor_argument(forge_block_t* block, size_t operation_index,
                                           size_t successor_index, const char* argument) {
    auto* resolved = resolve(block);
    if (!resolved || operation_index >= resolved->operations.size()) { set_error("operation is invalid"); return 0; }
    auto& operation = resolved->operations[operation_index];
    if (successor_index >= operation.successor_arguments.size()) { set_error("successor is invalid"); return 0; }
    try { operation.successor_arguments[successor_index].emplace_back(argument ? argument : ""); last_error.clear(); return 1; }
    catch (const std::exception& error) { last_error = error.what(); return 0; }
}

int forge_operation_set_alignment(forge_block_t* block, size_t operation_index, uint32_t alignment) {
    auto* resolved = resolve(block);
    if (!resolved || operation_index >= resolved->operations.size()) { set_error("operation is invalid"); return 0; }
    resolved->operations[operation_index].alignment = alignment; last_error.clear(); return 1;
}

int forge_operation_set_source_range(forge_block_t* block, size_t operation_index, const char* file,
                                     uint32_t line, uint32_t column,
                                     uint32_t end_line, uint32_t end_column) {
    auto* resolved = resolve(block);
    if (!resolved || operation_index >= resolved->operations.size()) { set_error("operation is invalid"); return 0; }
    auto& operation = resolved->operations[operation_index];
    operation.source_file = file ? file : ""; operation.source_line = line; operation.source_column = column;
    operation.source_end_line = end_line; operation.source_end_column = end_column;
    last_error.clear(); return 1;
}

int forge_operation_add_attribute(forge_block_t* block, size_t operation_index,
                                  const char* name, const char* value) {
    auto* resolved = resolve(block);
    if (!resolved || operation_index >= resolved->operations.size()) { set_error("operation is invalid"); return 0; }
    if (!name || *name == '\0') { set_error("attribute name is empty"); return 0; }
    try { resolved->operations[operation_index].attributes.push_back({name, value ? value : ""}); last_error.clear(); return 1; }
    catch (const std::exception& error) { last_error = error.what(); return 0; }
}

void forge_block_destroy(forge_block_t* block) { delete block; }
forge_builder_t* forge_builder_create(forge_context_t* context, forge_module_t* module) {
    if (context == nullptr || module == nullptr || module->value == nullptr) { set_error("context or module is null"); return nullptr; }
    try { last_error.clear(); return new forge_builder{std::make_unique<forge::ir::IRBuilder>(context->value, *module->value), {}}; }
    catch (...) { set_error("failed to create builder"); return nullptr; }
}

void forge_builder_destroy(forge_builder_t* builder) { delete builder; }
void forge_builder_position_at_end(forge_builder_t* builder, forge_block_t* block) {
    auto* resolved = resolve(block); if (builder == nullptr || builder->value == nullptr || resolved == nullptr) { set_error("builder or block is invalid"); return; }
    builder->value->position_at_end(*resolved); last_error.clear();
}

void forge_builder_clear_insertion_point(forge_builder_t* builder) { if (builder && builder->value) builder->value->clear_insertion_point(); }
void forge_builder_set_source_location(forge_builder_t* builder, const char* file, uint32_t line, uint32_t column) { if (builder && builder->value) builder->value->set_source_location({file ? file : "", line, column, line, column}); else set_error("builder is null"); }
void forge_builder_set_source_range(forge_builder_t* builder, const char* file, uint32_t line, uint32_t column, uint32_t end_line, uint32_t end_column) { if (builder && builder->value) builder->value->set_source_range(file ? file : "", line, column, end_line, end_column); else set_error("builder is null"); }
void forge_builder_clear_source_location(forge_builder_t* builder) { if (builder && builder->value) builder->value->clear_source_location(); }
void forge_module_set_metadata(forge_module_t* module, const char* name, const char* value) {
    if (!module || !module->value) { set_error("module is null"); return; }
    try {
        auto& metadata = module->value->metadata();
        const std::string key = name ? name : "";
        if (key.empty()) { set_error("metadata name must not be empty"); return; }
        const auto found = std::find_if(metadata.begin(), metadata.end(), [&](const forge::ir::Attribute& attribute) { return attribute.name == key; });
        if (found != metadata.end()) found->value = value ? value : "";
        else metadata.push_back({key, value ? value : ""});
        last_error.clear();
    } catch (const std::exception& error) { last_error = error.what(); }
}

const char* forge_module_get_metadata(const forge_module_t* module, const char* name) {
    if (!module || !module->value || !name) { set_error("module or metadata name is null"); return nullptr; }
    const auto& metadata = module->value->metadata();
    const auto found = std::find_if(metadata.begin(), metadata.end(), [&](const forge::ir::Attribute& attribute) { return attribute.name == name; });
    if (found == metadata.end()) { set_error("metadata key not found"); return nullptr; }
    last_error.clear();
    return found->value.c_str();
}

void forge_builder_set_next_attribute(forge_builder_t* builder, const char* name, const char* value) {
    if (!builder || !builder->value) { set_error("builder is null"); return; }
    try { builder->value->set_next_attribute(name ? name : "", value ? value : ""); last_error.clear(); }
    catch (const std::exception& error) { last_error = error.what(); }
}

void forge_builder_clear_next_attributes(forge_builder_t* builder) {
    if (!builder || !builder->value) { set_error("builder is null"); return; }
    builder->value->clear_next_attributes();
    last_error.clear();
}

const char* forge_function_parameter(const forge_function_t* function, size_t index) { if (function == nullptr || index >= function->parameter_names.size()) { set_error("parameter index out of range"); return nullptr; } return function->parameter_names[index].c_str(); }
const char* forge_block_parameter(const forge_block_t* block, size_t index) { if (block == nullptr || index >= block->parameter_names.size()) { set_error("block parameter index out of range"); return nullptr; } return block->parameter_names[index].c_str(); }
const char* forge_builder_constant(forge_builder_t* b, forge_type_kind_t t, const char* literal) { return result_call(b, [&]{ return b->value->create_constant(type_of(t), literal ? literal : "0"); }); }
const char* forge_builder_binary(forge_builder_t* b, forge_opcode_t op, forge_type_kind_t t, const char* lhs, const char* rhs) { return result_call(b, [&]{ return b->value->create_binary(opcode_of(op), type_of(t), lhs ? lhs : "", rhs ? rhs : ""); }); }
const char* forge_builder_compare(forge_builder_t* b, forge_opcode_t op, forge_type_kind_t t, const char* lhs, const char* rhs) { return result_call(b, [&]{ return b->value->create_compare(opcode_of(op), type_of(t), lhs ? lhs : "", rhs ? rhs : ""); }); }
const char* forge_builder_stack_alloc(forge_builder_t* b, uint64_t size, uint32_t alignment) { return result_call(b, [&]{ return b->value->create_stack_allocation(size, alignment); }); }
const char* forge_builder_load(forge_builder_t* b, forge_type_kind_t t, const char* address, uint32_t alignment) { return result_call(b, [&]{ return b->value->create_load(type_of(t), address ? address : "", alignment); }); }
void forge_builder_store(forge_builder_t* b, forge_type_kind_t t, const char* value, const char* address, uint32_t alignment) { if (!b || !b->value) { set_error("builder is null"); return; } try { b->value->create_store(type_of(t), value ? value : "", address ? address : "", alignment); last_error.clear(); } catch (const std::exception& e) { last_error=e.what(); } }
const char* forge_builder_call(forge_builder_t* b, forge_type_kind_t t, const char* callee, const char* const* args, size_t count) { return result_call(b, [&]{ return b->value->create_call(type_of(t), callee ? callee : "", string_args(args,count)); }); }
void forge_builder_jump(forge_builder_t* b, const forge_block_t* destination, const char* const* args, size_t count) { auto* d=resolve(destination); if (!b || !b->value || !d) { set_error("builder or destination block is invalid"); return; } try { b->value->create_jump(d->name,string_args(args,count)); last_error.clear(); } catch(const std::exception& e){last_error=e.what();} }
void forge_builder_branch(forge_builder_t* b, const char* condition, const forge_block_t* td, const forge_block_t* fd) { auto* t=resolve(td); auto* f=resolve(fd); if(!b||!b->value||!t||!f){set_error("builder or branch destination is invalid");return;} try{b->value->create_branch(condition?condition:"",t->name,f->name);last_error.clear();}catch(const std::exception&e){last_error=e.what();} }
void forge_builder_return(forge_builder_t* b, const char* value) { if (!b || !b->value) { set_error("builder is null"); return; } try { b->value->create_return(value ? value : ""); last_error.clear(); } catch (const std::exception& e) { last_error=e.what(); } }
void forge_builder_unreachable(forge_builder_t* b) { if(!b||!b->value){set_error("builder is null");return;} try{b->value->create_unreachable();last_error.clear();}catch(const std::exception&e){last_error=e.what();} }
int forge_module_verify(const forge_module_t* module, char* message, size_t capacity) {
    if (!module || !module->value) { set_error("module is null"); return 0; }
    module->diagnostics = forge::ir::verify_module(*module->value);
    if (module->diagnostics.empty()) { last_error.clear(); return 1; }
    last_error = module->diagnostics.front().message;
    if (message && capacity) {
        const auto n = std::min(capacity - 1, last_error.size());
        std::memcpy(message, last_error.data(), n);
        message[n] = '\0';
    }
    return 0;
}

int forge_module_optimize(forge_module_t* module, forge_optimization_level_t level) {
    if (!module || !module->value) { set_error("module is null"); return 0; }
    try {
        module->diagnostics = forge::ir::verify_module(*module->value);
        if (!module->diagnostics.empty()) {
            last_error = forge::diagnostics::render_all(module->diagnostics);
            return 0;
        }
        forge::pass::PassManager pipeline;
        forge::pass::build_standard_pipeline(pipeline, optimization_level_of(level));
        // The C API already verifies the complete module immediately before and
        // after the optimization pipeline. Verifying the whole module after
        // every function pass makes compiler-sized modules effectively O(F^2)
        // in verification work (hundreds of functions x every O1/O2 pass).
        // Keep per-pass verification available to dedicated pass tests, but do
        // not pay that redundant cost in production code generation.
        (void)pipeline.run(*module->value, false);
        module->diagnostics = forge::ir::verify_module(*module->value);
        if (!module->diagnostics.empty()) {
            last_error = forge::diagnostics::render_all(module->diagnostics);
            return 0;
        }
        last_error.clear();
        return 1;
    } catch (const std::exception& error) {
        last_error = error.what();
        return 0;
    } catch (...) {
        set_error("Forge optimization failed");
        return 0;
    }
}

namespace {
bool build_native_object_bytes(forge_module_t* module, forge_native_abi_t abi,
                               std::vector<std::byte>& bytes) {
    if (!module || !module->value) { set_error("module is null"); return false; }
    module->diagnostics = forge::ir::verify_module(*module->value);
    if (!module->diagnostics.empty()) {
        last_error = forge::diagnostics::render_all(module->diagnostics);
        return false;
    }
    auto lowered = forge::machine::lower_module(*module->value);
    if (!lowered.ok()) {
        module->diagnostics = std::move(lowered.diagnostics);
        last_error = forge::diagnostics::render_all(module->diagnostics);
        return false;
    }
    if (abi == FORGE_ABI_WINDOWS_X64) {
        auto object = forge::object::emit_coff_x86_64(
            *lowered.module, forge::codegen::x86_64::Abi::windows);
        if (!object.ok()) {
            module->diagnostics = std::move(object.diagnostics);
            last_error = forge::diagnostics::render_all(module->diagnostics);
            return false;
        }
        bytes = std::move(object.bytes);
    } else {
        auto object = forge::object::emit_elf64_x86_64(
            *lowered.module, forge::codegen::x86_64::Abi::system_v);
        if (!object.ok()) {
            module->diagnostics = std::move(object.diagnostics);
            last_error = forge::diagnostics::render_all(module->diagnostics);
            return false;
        }
        bytes = std::move(object.bytes);
    }
    last_error.clear();
    return true;
}
} // namespace

size_t forge_module_emit_object(forge_module_t* module, forge_native_abi_t abi,
                                uint8_t* output, size_t output_capacity) {
    try {
        std::vector<std::byte> bytes;
        if (!build_native_object_bytes(module, abi, bytes)) return 0;
        const size_t required = bytes.size();
        if (output != nullptr && output_capacity >= required && required != 0)
            std::memcpy(output, bytes.data(), required);
        if (output != nullptr && output_capacity < required) {
            set_error("object output buffer is too small");
            return required;
        }
        return required;
    } catch (const std::exception& error) {
        last_error = error.what();
        return 0;
    } catch (...) {
        set_error("Forge object emission failed");
        return 0;
    }
}

size_t forge_module_write_object_file(forge_module_t* module, forge_native_abi_t abi,
                                      const char* output_path) {
    if (output_path == nullptr || *output_path == '\0') {
        set_error("object output path is empty");
        return 0;
    }
    try {
        std::vector<std::byte> bytes;
        if (!build_native_object_bytes(module, abi, bytes)) return 0;
        std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
        if (!output) {
            set_error("unable to create object output file");
            return 0;
        }
        if (!bytes.empty())
            output.write(reinterpret_cast<const char*>(bytes.data()),
                         static_cast<std::streamsize>(bytes.size()));
        if (!output) {
            set_error("unable to write object output file");
            return 0;
        }
        last_error.clear();
        return bytes.size();
    } catch (const std::exception& error) {
        last_error = error.what();
        return 0;
    } catch (...) {
        set_error("Forge object file emission failed");
        return 0;
    }
}

size_t forge_module_diagnostic_count(const forge_module_t* module) {
    return module ? module->diagnostics.size() : 0;
}

forge_diagnostic_severity_t forge_module_diagnostic_severity(const forge_module_t* module, size_t index) {
    if (!module || index >= module->diagnostics.size()) { set_error("diagnostic index out of range"); return FORGE_DIAGNOSTIC_ERROR; }
    switch (module->diagnostics[index].severity) {
    case forge::DiagnosticSeverity::note: return FORGE_DIAGNOSTIC_NOTE;
    case forge::DiagnosticSeverity::warning: return FORGE_DIAGNOSTIC_WARNING;
    case forge::DiagnosticSeverity::error: return FORGE_DIAGNOSTIC_ERROR;
    }
    return FORGE_DIAGNOSTIC_ERROR;
}

const char* forge_module_diagnostic_message(const forge_module_t* module, size_t index) {
    if (!module || index >= module->diagnostics.size()) { set_error("diagnostic index out of range"); return nullptr; }
    last_error.clear();
    return module->diagnostics[index].message.c_str();
}

const char* forge_module_diagnostic_file(const forge_module_t* module, size_t index) {
    if (!module || index >= module->diagnostics.size()) { set_error("diagnostic index out of range"); return nullptr; }
    last_error.clear();
    return module->diagnostics[index].source_file.c_str();
}

uint32_t forge_module_diagnostic_line(const forge_module_t* module, size_t index) {
    if (!module || index >= module->diagnostics.size()) { set_error("diagnostic index out of range"); return 0; }
    return static_cast<uint32_t>(module->diagnostics[index].source_line);
}

uint32_t forge_module_diagnostic_column(const forge_module_t* module, size_t index) {
    if (!module || index >= module->diagnostics.size()) { set_error("diagnostic index out of range"); return 0; }
    return static_cast<uint32_t>(module->diagnostics[index].source_column);
}

uint32_t forge_module_diagnostic_end_line(const forge_module_t* module, size_t index) {
    if (!module || index >= module->diagnostics.size()) { set_error("diagnostic index out of range"); return 0; }
    return static_cast<uint32_t>(module->diagnostics[index].source_end_line);
}

uint32_t forge_module_diagnostic_end_column(const forge_module_t* module, size_t index) {
    if (!module || index >= module->diagnostics.size()) { set_error("diagnostic index out of range"); return 0; }
    return static_cast<uint32_t>(module->diagnostics[index].source_end_column);
}

int forge_builder_has_insertion_point(const forge_builder_t* builder) {
    return builder && builder->value && builder->value->has_insertion_point() ? 1 : 0;
}

int forge_builder_insertion_block_terminated(const forge_builder_t* builder) {
    return builder && builder->value && builder->value->insertion_block_terminated() ? 1 : 0;
}

size_t forge_module_print(const forge_module_t* module, char* output, size_t capacity) { if(!module||!module->value){set_error("module is null");return 0;} auto text=forge::ir::print_module(*module->value); size_t required=text.size()+1; if(output&&capacity){auto n=std::min(capacity-1,text.size());std::memcpy(output,text.data(),n);output[n]='\0';} last_error.clear(); return required; }
size_t forge_module_source_map_json(const forge_module_t* module, char* output, size_t capacity) {
    if (!module || !module->value) { set_error("module is null"); return 0; }
    const auto text = forge::ir::build_source_map_json(*module->value);
    const size_t required = text.size() + 1;
    if (output && capacity) {
        const auto count = std::min(capacity - 1, text.size());
        std::memcpy(output, text.data(), count);
        output[count] = '\0';
    }
    last_error.clear();
    return required;
}

size_t forge_module_semantic_fingerprint(const forge_module_t* module, char* output, size_t capacity) {
    if (!module || !module->value) { set_error("module is null"); return 0; }
    const auto text = forge::ir::build_incremental_snapshot(*module->value).semantic_fingerprint;
    const size_t required = text.size() + 1;
    if (output && capacity) { const auto count = std::min(capacity - 1, text.size()); std::memcpy(output, text.data(), count); output[count] = '\0'; }
    last_error.clear();
    return required;
}

size_t forge_module_frontend_fingerprint(const forge_module_t* module, char* output, size_t capacity) {
    if (!module || !module->value) { set_error("module is null"); return 0; }
    const auto text = forge::ir::build_incremental_snapshot(*module->value).frontend_fingerprint;
    const size_t required = text.size() + 1;
    if (output && capacity) { const auto count = std::min(capacity - 1, text.size()); std::memcpy(output, text.data(), count); output[count] = '\0'; }
    last_error.clear();
    return required;
}

size_t forge_module_incremental_manifest_json(const forge_module_t* module, char* output, size_t capacity) {
    if (!module || !module->value) { set_error("module is null"); return 0; }
    const auto text = forge::ir::build_incremental_manifest_json(forge::ir::build_incremental_snapshot(*module->value));
    const size_t required = text.size() + 1;
    if (output && capacity) { const auto count = std::min(capacity - 1, text.size()); std::memcpy(output, text.data(), count); output[count] = '\0'; }
    last_error.clear();
    return required;
}

size_t forge_module_cache_key(const forge_module_t* module, const char* frontend_id,
                              const char* configuration, char* output, size_t capacity) {
    if (!module || !module->value) { set_error("module is null"); return 0; }
    const auto text = forge::ir::build_cache_key(*module->value, frontend_id ? frontend_id : "", configuration ? configuration : "");
    const size_t required = text.size() + 1;
    if (output && capacity) { const auto count = std::min(capacity - 1, text.size()); std::memcpy(output, text.data(), count); output[count] = '\0'; }
    last_error.clear();
    return required;
}

size_t forge_module_incremental_build_plan_json(const forge_module_t* previous_module,
                                                const forge_module_t* current_module,
                                                const char* frontend_id,
                                                const char* configuration,
                                                char* output, size_t capacity) {
    if (!previous_module || !previous_module->value || !current_module || !current_module->value) {
        set_error("previous or current module is null");
        return 0;
    }
    const auto previous = forge::ir::build_incremental_snapshot(*previous_module->value);
    const auto current = forge::ir::build_incremental_snapshot(*current_module->value);
    const auto plan = forge::ir::build_incremental_build_plan(
        previous, current, frontend_id ? frontend_id : "", configuration ? configuration : "");
    const auto text = forge::ir::build_incremental_build_plan_json(plan);
    const size_t required = text.size() + 1;
    if (output && capacity) {
        const auto count = std::min(capacity - 1, text.size());
        std::memcpy(output, text.data(), count);
        output[count] = '\0';
    }
    last_error.clear();
    return required;
}

size_t forge_module_parallel_build_schedule_json(const forge_module_t* previous_module,
                                                 const forge_module_t* current_module,
                                                 const char* frontend_id,
                                                 const char* configuration,
                                                 size_t requested_workers,
                                                 char* output, size_t capacity) {
    if (!previous_module || !previous_module->value || !current_module || !current_module->value) {
        set_error("previous or current module is null");
        return 0;
    }
    const auto previous = forge::ir::build_incremental_snapshot(*previous_module->value);
    const auto current = forge::ir::build_incremental_snapshot(*current_module->value);
    const auto plan = forge::ir::build_incremental_build_plan(
        previous, current, frontend_id ? frontend_id : "", configuration ? configuration : "");
    const auto schedule = forge::ir::build_parallel_build_schedule(plan, requested_workers);
    const auto text = forge::ir::build_parallel_build_schedule_json(schedule);
    const size_t required = text.size() + 1;
    if (output && capacity) {
        const auto count = std::min(capacity - 1, text.size());
        std::memcpy(output, text.data(), count);
        output[count] = '\0';
    }
    last_error.clear();
    return required;
}

size_t forge_module_function_abi_json(const forge_module_t* module,
                                      const char* function_name,
                                      forge_native_abi_t abi,
                                      char* output, size_t capacity) {
    if (!module || !module->value || !function_name) { set_error("module or function name is null"); return 0; }
    const auto found = std::find_if(module->value->functions().begin(), module->value->functions().end(),
        [&](const forge::ir::Function& function) { return function.name == function_name; });
    if (found == module->value->functions().end()) { set_error("function not found"); return 0; }
    const auto native_abi = abi == FORGE_ABI_WINDOWS_X64 ? forge::target::NativeAbi::windows_x64
                                                         : forge::target::NativeAbi::system_v_x86_64;
    const auto classified = forge::target::classify_function(*module->value, *found, native_abi);
    std::ostringstream json;
    json << "{\"function\":\"" << found->name << "\",\"variadic\":" << (classified.variadic ? "true" : "false")
         << ",\"integerRegisters\":" << classified.integer_registers
         << ",\"floatingRegisters\":" << classified.floating_registers
         << ",\"stackBytes\":" << classified.stack_bytes
         << ",\"parameters\":[";
    for (size_t index = 0; index < classified.parameters.size(); ++index) {
        if (index) json << ',';
        const auto& parameter = classified.parameters[index];
        json << "{\"size\":" << parameter.size << ",\"alignment\":" << parameter.alignment
             << ",\"registerCount\":" << static_cast<unsigned>(parameter.register_count)
             << ",\"indirect\":" << (parameter.passed_indirectly ? "true" : "false")
             << ",\"classes\":[\"" << forge::target::abi_value_class_name(parameter.classes[0])
             << "\",\"" << forge::target::abi_value_class_name(parameter.classes[1]) << "\"]}";
    }
    json << "]}";
    last_error.clear();
    return copy_text(json.str(), output, capacity);
}

size_t forge_module_dependency_build_schedule_json(const forge_module_t* previous_module,
                                                   const forge_module_t* current_module,
                                                   const char* frontend_id,
                                                   const char* configuration,
                                                   size_t requested_workers,
                                                   char* output, size_t capacity) {
    if (!previous_module || !previous_module->value || !current_module || !current_module->value) {
        set_error("previous or current module is null");
        return 0;
    }
    const auto previous = forge::ir::build_incremental_snapshot(*previous_module->value);
    const auto current = forge::ir::build_incremental_snapshot(*current_module->value);
    auto plan = forge::ir::build_incremental_build_plan(
        previous, current, frontend_id ? frontend_id : "", configuration ? configuration : "");
    const auto graph = forge::ir::build_function_dependency_graph(*current_module->value);
    plan = forge::ir::propagate_dependency_invalidations(
        plan, graph, frontend_id ? frontend_id : "", configuration ? configuration : "");
    const auto schedule = forge::ir::build_dependency_build_schedule(plan, graph, requested_workers);
    const auto text = forge::ir::build_dependency_build_schedule_json(schedule);
    const size_t required = text.size() + 1;
    if (output && capacity) {
        const auto count = std::min(capacity - 1, text.size());
        std::memcpy(output, text.data(), count);
        output[count] = '\0';
    }
    last_error.clear();
    return required;
}

const char* forge_last_error(void) { return last_error.c_str(); }
void forge_clear_error(void) { last_error.clear(); }
}
