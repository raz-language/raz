// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/ir/verifier.hpp"
#include "forge/platform/data_layout.hpp"
#include <algorithm>
#include <charconv>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace forge::ir {
Diagnostics verify_module(const Module& module) {
    Diagnostics diagnostics;
    const auto data_layout = target::DataLayout::host();
    std::unordered_set<std::string> array_names;
    std::unordered_map<std::string, const ArrayDecl*> array_table;
    for (const auto& array : module.arrays()) {
        if (array.name.empty() || !array_names.insert(array.name).second)
            diagnostics.push_back({DiagnosticSeverity::error, "duplicate or empty array @" + array.name, {}});
        array_table.emplace(array.name, &array);
        if (array.element_count == 0 || !data_layout.array_layout(module, array).has_value())
            diagnostics.push_back({DiagnosticSeverity::error, "invalid array layout for @" + array.name, {}});
    }
    std::unordered_set<std::string> struct_names;
    std::unordered_map<std::string, const StructDecl*> struct_table;
    for (const auto& structure : module.structs()) {
        if (structure.name.empty() || !struct_names.insert(structure.name).second)
            diagnostics.push_back({DiagnosticSeverity::error, "duplicate or empty structure @" + structure.name, {}});
        struct_table.emplace(structure.name, &structure);
    }

    for (const auto& array : module.arrays()) {
        if (array.element_aggregate_kind == AggregateRefKind::structure && !struct_table.contains(array.element_aggregate_name))
            diagnostics.push_back({DiagnosticSeverity::error, "unknown structure @" + array.element_aggregate_name + " in array @" + array.name, {}});
        if (array.element_aggregate_kind == AggregateRefKind::array && !array_table.contains(array.element_aggregate_name))
            diagnostics.push_back({DiagnosticSeverity::error, "unknown array @" + array.element_aggregate_name + " in array @" + array.name, {}});
        if (array.element_aggregate_kind == AggregateRefKind::scalar && !data_layout.size_of(array.element_type).has_value())
            diagnostics.push_back({DiagnosticSeverity::error, "unsized element type in array @" + array.name, {}});
    }

    for (const auto& structure : module.structs()) {
        std::unordered_set<std::string> field_names;
        for (const auto& field : structure.fields) {
            if (field.name.empty() || !field_names.insert(field.name).second)
                diagnostics.push_back({DiagnosticSeverity::error, "duplicate or empty field in structure @" + structure.name, {}});
            if (field.aggregate_kind == AggregateRefKind::scalar && !data_layout.size_of(field.type).has_value())
                diagnostics.push_back({DiagnosticSeverity::error, "unsized field " + field.name + " in structure @" + structure.name, {}});
            if (field.aggregate_kind == AggregateRefKind::structure && !struct_table.contains(field.aggregate_name))
                diagnostics.push_back({DiagnosticSeverity::error, "unknown structure @" + field.aggregate_name + " in field " + field.name, {}});
            if (field.aggregate_kind == AggregateRefKind::array && !array_table.contains(field.aggregate_name))
                diagnostics.push_back({DiagnosticSeverity::error, "unknown array @" + field.aggregate_name + " in field " + field.name, {}});
        }
        if (!data_layout.struct_layout(module, structure).has_value())
            diagnostics.push_back({DiagnosticSeverity::error, "structure layout failed or recursive aggregate detected for @" + structure.name, {}});
    }
    const auto aggregate_is_move_only = [&](AggregateRefKind kind, const std::string& name) {
        if (kind == AggregateRefKind::structure) {
            const auto found = struct_table.find(name);
            return found != struct_table.end() && found->second->move_only;
        }
        if (kind == AggregateRefKind::array) {
            const auto found = array_table.find(name);
            return found != array_table.end() && found->second->move_only;
        }
        return false;
    };
    for (const auto& array : module.arrays()) {
        if (array.has_aggregate_elements() && aggregate_is_move_only(array.element_aggregate_kind, array.element_aggregate_name) && !array.move_only)
            diagnostics.push_back({DiagnosticSeverity::error, "array @" + array.name + " contains move-only aggregate elements and must also be moveonly", {}});
    }

    for (const auto& structure : module.structs()) {
        for (const auto& field : structure.fields) {
            if (field.is_aggregate() && aggregate_is_move_only(field.aggregate_kind, field.aggregate_name) && !structure.move_only)
                diagnostics.push_back({DiagnosticSeverity::error, "structure @" + structure.name + " contains move-only field " + field.name + " and must also be moveonly", {}});
        }
    }

    std::unordered_set<std::string> global_names;
    for (const auto& global : module.globals()) {
        if (global.is_external && global.linkage == SymbolLinkage::internal)
            diagnostics.push_back({DiagnosticSeverity::error, "external global @" + global.name + " cannot have internal linkage", {}});
        if (global.is_thread_local && global.is_constant)
            diagnostics.push_back({DiagnosticSeverity::error, "thread-local global @" + global.name + " cannot be constant", {}});
        if (!global_names.insert(global.name).second)
            diagnostics.push_back({DiagnosticSeverity::error, "duplicate global @" + global.name, {}});
        std::optional<std::size_t> aggregate_size;
        std::optional<std::size_t> aggregate_alignment;
        if (global.is_named_aggregate()) {
            aggregate_size = data_layout.aggregate_size(module, global.aggregate_kind, global.aggregate_name);
            aggregate_alignment = data_layout.aggregate_alignment(module, global.aggregate_kind, global.aggregate_name);
            if (!aggregate_size || !aggregate_alignment)
                diagnostics.push_back({DiagnosticSeverity::error, "unknown or invalid aggregate type for global @" + global.name, {}});
        } else {
            if (!global.type.is_integer() && global.function_signature_name.empty())
                diagnostics.push_back({DiagnosticSeverity::error, "global @" + global.name + " must have integer, callback, or named aggregate type", {}});
            if (global.element_count == 0)
                diagnostics.push_back({DiagnosticSeverity::error, "global @" + global.name + " cannot have zero elements", {}});
            if (global.element_count != 1 && global.type != Type(TypeKind::i8) && global.function_signature_name.empty())
                diagnostics.push_back({DiagnosticSeverity::error, "byte-array global @" + global.name + " requires i8 elements", {}});
        }
        if (global.alignment != 0 && (global.alignment > 4096 || (global.alignment & (global.alignment - 1U)) != 0))
            diagnostics.push_back({DiagnosticSeverity::error, "global @" + global.name + " requires power-of-two alignment no greater than 4096", {}});
        const bool has_initializer = !global.initializer.empty() || global.zero_initialized || !global.bytes.empty();
        if (global.is_external && has_initializer)
            diagnostics.push_back({DiagnosticSeverity::error, "external global @" + global.name + " cannot have an initializer", {}});
        if (!global.is_external && !has_initializer)
            diagnostics.push_back({DiagnosticSeverity::error, "global @" + global.name + " requires an initializer", {}});
        const auto expected_bytes = global.is_named_aggregate() ? aggregate_size.value_or(0)
            : (!global.function_signature_name.empty() ? static_cast<std::size_t>(global.element_count) * sizeof(std::uintptr_t)
               : static_cast<std::size_t>(global.element_count));
        if (!global.bytes.empty() && !global.is_named_aggregate() && global.element_count == 1 && global.type != Type(TypeKind::i8))
            diagnostics.push_back({DiagnosticSeverity::error, "scalar global @" + global.name + " cannot use a byte-string initializer", {}});
        if (!global.bytes.empty() && global.bytes.size() != expected_bytes)
            diagnostics.push_back({DiagnosticSeverity::error, "byte initializer size mismatch for global @" + global.name, {}});
        if (global.zero_initialized && !global.is_named_aggregate() && global.element_count == 1 && global.type != Type(TypeKind::i8) && global.function_signature_name.empty())
            diagnostics.push_back({DiagnosticSeverity::error, "scalar integer global @" + global.name + " requires an integer initializer", {}});
        if (global.is_named_aggregate() && !global.zero_initialized && global.bytes.empty() && !global.is_external)
            diagnostics.push_back({DiagnosticSeverity::error, "aggregate global @" + global.name + " requires zero or exact byte initializer", {}});
    }
    std::unordered_set<std::string> function_names;
    std::unordered_map<std::string, const Function*> function_table;
    for (const auto& function : module.functions()) function_table.emplace(function.name, &function);

    for (const auto& global : module.globals()) {
        if (!global.function_signature_name.empty()) {
            if (global.type != Type(TypeKind::ptr) || global.aggregate_kind != AggregateRefKind::scalar || global.element_count == 0)
                diagnostics.push_back({DiagnosticSeverity::error, "callback global @" + global.name + " must contain one or more ptr elements", {}});
            const auto signature = function_table.find(global.function_signature_name);
            if (signature == function_table.end() || !signature->second->is_signature)
                diagnostics.push_back({DiagnosticSeverity::error, "callback global @" + global.name + " references unknown signature @" + global.function_signature_name, {}});
        }
    }

    for (const auto& function : module.functions()) {
        if (!function_names.insert(function.name).second) {
            diagnostics.push_back({DiagnosticSeverity::error, "duplicate function @" + function.name, {}});
        }
        const auto verify_value_declaration = [&](const ValueDecl& value, std::string_view context) {
            if (!value.function_signature_name.empty()) {
                if (value.type != Type(TypeKind::ptr) || value.aggregate_kind != AggregateRefKind::scalar)
                    diagnostics.push_back({DiagnosticSeverity::error, std::string(context) + " callback value must lower as scalar ptr", {}});
                const auto signature = function_table.find(value.function_signature_name);
                if (signature == function_table.end() || !signature->second->is_signature)
                    diagnostics.push_back({DiagnosticSeverity::error, std::string(context) + " references unknown callback signature @" + value.function_signature_name, {}});
                if (value.owned || value.borrow_mode != BorrowMode::none)
                    diagnostics.push_back({DiagnosticSeverity::error, std::string(context) + " callback value cannot be owned or borrowed", {}});
                return;
            }
            if (value.aggregate_kind == AggregateRefKind::scalar) return;
            if (value.type != Type(TypeKind::ptr))
                diagnostics.push_back({DiagnosticSeverity::error, std::string(context) + " aggregate value must lower as ptr", {}});
            if (value.aggregate_kind == AggregateRefKind::structure && !struct_table.contains(value.aggregate_name))
                diagnostics.push_back({DiagnosticSeverity::error, std::string(context) + " references unknown structure @" + value.aggregate_name, {}});
            if (value.aggregate_kind == AggregateRefKind::array && !array_table.contains(value.aggregate_name))
                diagnostics.push_back({DiagnosticSeverity::error, std::string(context) + " references unknown array @" + value.aggregate_name, {}});
        };
        if (function.is_external && function.linkage == SymbolLinkage::internal)
            diagnostics.push_back({DiagnosticSeverity::error, "external function @" + function.name + " cannot have internal linkage", {}});
        if (function.variadic && !function.is_external && !function.is_signature)
            diagnostics.push_back({DiagnosticSeverity::error, "variadic function @" + function.name + " must be an external declaration or signature", {}});
        if (function.calling_convention == CallingConvention::fast && function.variadic)
            diagnostics.push_back({DiagnosticSeverity::error, "variadic function @" + function.name + " cannot use the fast calling convention", {}});
        if (function.return_owned && !function.returns_aggregate())
            diagnostics.push_back({DiagnosticSeverity::error, "owned return from @" + function.name + " requires a named aggregate", {}});
        if (function.return_owned && function.return_borrow_mode != BorrowMode::none)
            diagnostics.push_back({DiagnosticSeverity::error, "return from @" + function.name + " cannot be both owned and borrowed", {}});
        if (function.return_borrow_mode != BorrowMode::none && !function.returns_aggregate())
            diagnostics.push_back({DiagnosticSeverity::error, "borrowed return from @" + function.name + " requires a named aggregate", {}});
        if (function.return_borrow_mode == BorrowMode::none && function.return_borrow_parameter != -1)
            diagnostics.push_back({DiagnosticSeverity::error, "non-borrowed return from @" + function.name + " cannot declare a borrow source", {}});
        if (function.return_borrow_mode != BorrowMode::none) {
            if (function.return_borrow_parameter < 0 || static_cast<std::size_t>(function.return_borrow_parameter) >= function.parameters.size()) {
                diagnostics.push_back({DiagnosticSeverity::error, "borrowed return from @" + function.name + " references an invalid parameter index", {}});
            } else {
                const auto& source = function.parameters[static_cast<std::size_t>(function.return_borrow_parameter)];
                if (source.borrow_mode == BorrowMode::none || !source.is_aggregate())
                    diagnostics.push_back({DiagnosticSeverity::error, "borrowed return source in @" + function.name + " must be a borrowed aggregate parameter", {}});
                else if (source.aggregate_kind != function.return_aggregate_kind || source.aggregate_name != function.return_aggregate_name)
                    diagnostics.push_back({DiagnosticSeverity::error, "borrowed return source aggregate mismatch in @" + function.name, {}});
                else if (function.return_borrow_mode == BorrowMode::mutable_ && source.borrow_mode != BorrowMode::mutable_)
                    diagnostics.push_back({DiagnosticSeverity::error, "mutable borrowed return from @" + function.name + " requires a mutable borrowed source", {}});
            }
        }
        if (function.return_owned && function.returns_aggregate() && aggregate_is_move_only(function.return_aggregate_kind, function.return_aggregate_name))
            diagnostics.push_back({DiagnosticSeverity::error, "owned return from @" + function.name + " would copy move-only aggregate @" + function.return_aggregate_name, {}});
        if (function.returns_aggregate()) {
            if (function.return_type != Type(TypeKind::ptr))
                diagnostics.push_back({DiagnosticSeverity::error, "aggregate return from @" + function.name + " must lower as ptr", {}});
            if (function.return_aggregate_kind == AggregateRefKind::structure && !struct_table.contains(function.return_aggregate_name))
                diagnostics.push_back({DiagnosticSeverity::error, "function @" + function.name + " returns unknown structure @" + function.return_aggregate_name, {}});
            if (function.return_aggregate_kind == AggregateRefKind::array && !array_table.contains(function.return_aggregate_name))
                diagnostics.push_back({DiagnosticSeverity::error, "function @" + function.name + " returns unknown array @" + function.return_aggregate_name, {}});
        }
        for (const auto& parameter : function.parameters) {
            verify_value_declaration(parameter, "function parameter " + parameter.name);
            if (parameter.owned && !parameter.is_aggregate())
                diagnostics.push_back({DiagnosticSeverity::error, "owned parameter " + parameter.name + " in @" + function.name + " requires a named aggregate", {}});
            if (parameter.borrow_mode != BorrowMode::none && !parameter.is_aggregate())
                diagnostics.push_back({DiagnosticSeverity::error, "borrowed parameter " + parameter.name + " in @" + function.name + " requires a named aggregate", {}});
            if (parameter.owned && parameter.borrow_mode != BorrowMode::none)
                diagnostics.push_back({DiagnosticSeverity::error, "parameter " + parameter.name + " in @" + function.name + " cannot be both owned and borrowed", {}});
            if (parameter.owned && parameter.is_aggregate() && aggregate_is_move_only(parameter.aggregate_kind, parameter.aggregate_name))
                diagnostics.push_back({DiagnosticSeverity::error, "owned parameter " + parameter.name + " in @" + function.name + " would copy move-only aggregate @" + parameter.aggregate_name, {}});
        }
        struct ReturnBorrowOrigin {
            std::size_t parameter_index{};
            BorrowMode mode{BorrowMode::none};
            std::string path;
        };
        std::unordered_map<std::string, ReturnBorrowOrigin> return_borrow_origins;
        for (std::size_t index = 0; index < function.parameters.size(); ++index)
            if (function.parameters[index].borrow_mode != BorrowMode::none)
                return_borrow_origins.emplace(function.parameters[index].name, ReturnBorrowOrigin{index, function.parameters[index].borrow_mode, function.parameters[index].name});
        const auto propagates_return_borrow = [](const std::string& opcode) {
            return opcode == "ptr.offset" || opcode == "field.address" ||
                   opcode == "struct.field.address" || opcode == "struct.field.name.address" ||
                   opcode == "array.element.address" || opcode == "callback.element.address" || opcode == "aggregate.move.struct" ||
                   opcode == "aggregate.move.array" || opcode == "aggregate.borrow.struct" ||
                   opcode == "aggregate.borrow.array" || opcode == "aggregate.borrow.mut.struct" ||
                   opcode == "aggregate.borrow.mut.array";
        };
        bool return_origin_changed = true;
        while (return_origin_changed) {
            return_origin_changed = false;
            for (const auto& block : function.blocks) for (const auto& operation : block.operations) {
                if (operation.result.empty() || return_borrow_origins.contains(operation.result)) continue;
                if (propagates_return_borrow(operation.opcode) && !operation.operands.empty()) {
                    if (const auto source = return_borrow_origins.find(operation.operands[0]); source != return_borrow_origins.end()) {
                        auto origin = source->second;
                        origin.path += " -> " + operation.result + " (" + operation.opcode + ")";
                        if (operation.opcode.starts_with("aggregate.borrow.mut")) origin.mode = BorrowMode::mutable_;
                        else if (operation.opcode.starts_with("aggregate.borrow.")) origin.mode = BorrowMode::immutable;
                        return_borrow_origins.emplace(operation.result, origin);
                        return_origin_changed = true;
                    }
                } else if ((operation.opcode == "call" || operation.opcode == "call.indirect") && !operation.operands.empty()) {
                    const bool indirect = operation.opcode == "call.indirect";
                    const std::size_t signature_index = indirect ? 1U : 0U;
                    if (operation.operands.size() <= signature_index || !operation.operands[signature_index].starts_with("@")) continue;
                    const auto callee = function_table.find(operation.operands[signature_index].substr(1));
                    if (callee != function_table.end() && callee->second->return_borrow_mode != BorrowMode::none &&
                        callee->second->return_borrow_parameter >= 0) {
                        const auto argument_index = static_cast<std::size_t>(callee->second->return_borrow_parameter) + (indirect ? 2U : 1U);
                        if (argument_index < operation.operands.size()) {
                            if (const auto source = return_borrow_origins.find(operation.operands[argument_index]); source != return_borrow_origins.end()) {
                                auto origin = source->second;
                                origin.mode = callee->second->return_borrow_mode;
                                origin.path += " -> " + operation.result + (indirect ? " (call.indirect as @" : " (call @") + callee->second->name + ")";
                                return_borrow_origins.emplace(operation.result, origin);
                                return_origin_changed = true;
                            }
                        }
                    }
                }
            }
        }

        for (const auto& block : function.blocks)
            for (const auto& parameter : block.parameters) {
                verify_value_declaration(parameter, "block parameter " + parameter.name);
                if (parameter.owned && !parameter.is_aggregate())
                    diagnostics.push_back({DiagnosticSeverity::error, "owned block parameter " + parameter.name + " requires a named aggregate", {}});
                if (parameter.borrow_mode != BorrowMode::none)
                    diagnostics.push_back({DiagnosticSeverity::error, "borrow modes are only valid on function parameters; block parameter " + parameter.name + " is invalid", {}});
                if (parameter.owned && parameter.is_aggregate() && aggregate_is_move_only(parameter.aggregate_kind, parameter.aggregate_name))
                    diagnostics.push_back({DiagnosticSeverity::error, "owned block parameter " + parameter.name + " would copy move-only aggregate @" + parameter.aggregate_name, {}});
            }
        if (function.is_signature) {
            if (function.is_external) diagnostics.push_back({DiagnosticSeverity::error, "signature @" + function.name + " cannot be external", {}});
            if (!function.blocks.empty()) diagnostics.push_back({DiagnosticSeverity::error, "signature @" + function.name + " cannot have blocks", {}});
            continue;
        }
        if (function.is_external) {
            if (!function.blocks.empty()) diagnostics.push_back({DiagnosticSeverity::error, "external function @" + function.name + " cannot have blocks", {}});
            continue;
        }
        if (function.blocks.empty()) { diagnostics.push_back({DiagnosticSeverity::error, "function @" + function.name + " has no blocks", {}}); continue; }
        for (const auto& block : function.blocks) {
            for (const auto& operation : block.operations) {
                if (operation.opcode == "call" && !operation.operands.empty() && operation.operands[0].starts_with("@")) {
                    const auto found = function_table.find(operation.operands[0].substr(1));
                    if (found != function_table.end() && found->second->is_signature)
                        diagnostics.push_back({DiagnosticSeverity::error, "cannot directly call signature " + operation.operands[0], {}});
                }
                if ((operation.opcode == "func.address" || operation.opcode == "callback.address") && !operation.operands.empty() && operation.operands[0].starts_with("@")) {
                    const auto addressed = function_table.find(operation.operands[0].substr(1));
                    if (addressed != function_table.end() && addressed->second->is_signature)
                        diagnostics.push_back({DiagnosticSeverity::error, "cannot take address of signature " + operation.operands[0], {}});
                    if (operation.operands.size() == 2 && operation.operands[1].starts_with("@")) {
                        const auto signature = function_table.find(operation.operands[1].substr(1));
                        if (signature == function_table.end() || !signature->second->is_signature)
                            diagnostics.push_back({DiagnosticSeverity::error, "func.address as target must name a signature declaration", {}});
                        else if (addressed != function_table.end()) {
                            const auto compatible = [&](const Function& a, const Function& b) {
                                if (a.return_type != b.return_type || a.return_aggregate_kind != b.return_aggregate_kind ||
                                    a.return_aggregate_name != b.return_aggregate_name || a.return_owned != b.return_owned ||
                                    a.return_borrow_mode != b.return_borrow_mode || a.return_borrow_parameter != b.return_borrow_parameter ||
                                    a.variadic != b.variadic || a.calling_convention != b.calling_convention ||
                                    a.parameters.size() != b.parameters.size()) return false;
                                for (std::size_t i=0;i<a.parameters.size();++i) {
                                    const auto& x=a.parameters[i]; const auto& y=b.parameters[i];
                                    if (x.type!=y.type || x.aggregate_kind!=y.aggregate_kind || x.aggregate_name!=y.aggregate_name ||
                                        x.owned!=y.owned || x.borrow_mode!=y.borrow_mode) return false;
                                }
                                return true;
                            };
                            if (!compatible(*addressed->second, *signature->second))
                                diagnostics.push_back({DiagnosticSeverity::error, "function " + operation.operands[0] + " is incompatible with signature " + operation.operands[1], {}});
                        }
                    }
                }
                if ((operation.opcode == "aggregate.copy.struct" || operation.opcode == "aggregate.copy.array") && operation.operands.size() >= 3) {
                    const auto kind = operation.opcode == "aggregate.copy.struct" ? AggregateRefKind::structure : AggregateRefKind::array;
                    const auto name = operation.operands[2].starts_with("@") ? operation.operands[2].substr(1) : operation.operands[2];
                    if (aggregate_is_move_only(kind, name))
                        diagnostics.push_back({DiagnosticSeverity::error, "cannot copy move-only aggregate @" + name, {}});
                }
            }
        }

        const std::size_t block_count = function.blocks.size();
        std::unordered_map<std::string, std::size_t> block_index;
        for (std::size_t i = 0; i < block_count; ++i) {
            if (!block_index.emplace(function.blocks[i].name, i).second) {
                diagnostics.push_back({DiagnosticSeverity::error, "duplicate block " + function.blocks[i].name, {}});
            }
        }

        std::vector<std::vector<std::size_t>> predecessors(block_count);
        for (std::size_t i = 0; i < block_count; ++i) {
            for (const auto& operation : function.blocks[i].operations) {
                for (const auto& successor : operation.successors) {
                    const auto found = block_index.find(successor);
                    if (found != block_index.end()) predecessors[found->second].push_back(i);
                }
            }
        }

        // Propagate borrowed-return origins through block parameters and then through
        // operations again. A block parameter receives provenance only when every incoming
        // edge supplies a value derived from the same borrowed function parameter. This
        // permits conditional borrowed returns while rejecting joins of unrelated loans.
        bool block_return_origin_changed = true;
        while (block_return_origin_changed) {
            block_return_origin_changed = false;
            for (std::size_t target_index = 0; target_index < block_count; ++target_index) {
                const auto& target = function.blocks[target_index];
                for (std::size_t parameter_index = 0; parameter_index < target.parameters.size(); ++parameter_index) {
                    const auto& parameter = target.parameters[parameter_index];
                    if (predecessors[target_index].empty()) continue;
                    std::optional<ReturnBorrowOrigin> candidate;
                    bool conflict = false;
                    bool saw_edge = false;
                    for (const auto predecessor_index : predecessors[target_index]) {
                        const auto& predecessor = function.blocks[predecessor_index];
                        if (predecessor.operations.empty()) continue;
                        const auto& terminator = predecessor.operations.back();
                        for (std::size_t edge = 0; edge < terminator.successors.size(); ++edge) {
                            if (terminator.successors[edge] != target.name ||
                                edge >= terminator.successor_arguments.size() ||
                                parameter_index >= terminator.successor_arguments[edge].size()) continue;
                            saw_edge = true;
                            const auto incoming = return_borrow_origins.find(terminator.successor_arguments[edge][parameter_index]);
                            if (incoming == return_borrow_origins.end()) continue;
                            if (!candidate.has_value()) candidate = incoming->second;
                            else {
                                if (candidate->parameter_index != incoming->second.parameter_index) {
                                    conflict = true;
                                    break;
                                }
                                if (incoming->second.mode == BorrowMode::immutable) candidate->mode = BorrowMode::immutable;
                            }
                        }
                        if (conflict) break;
                    }
                    if (!saw_edge || conflict || !candidate.has_value()) {
                        if (conflict && return_borrow_origins.erase(parameter.name) != 0)
                            block_return_origin_changed = true;
                        continue;
                    }
                    candidate->path += " -> " + parameter.name + " (block " + target.name + ")";
                    const auto existing = return_borrow_origins.find(parameter.name);
                    if (existing == return_borrow_origins.end()) {
                        return_borrow_origins.emplace(parameter.name, *candidate);
                        block_return_origin_changed = true;
                    } else if (existing->second.parameter_index != candidate->parameter_index ||
                               existing->second.mode != candidate->mode) {
                        existing->second = *candidate;
                        block_return_origin_changed = true;
                    }
                }
            }
            for (const auto& block : function.blocks) for (const auto& operation : block.operations) {
                if (operation.result.empty() || return_borrow_origins.contains(operation.result)) continue;
                if (propagates_return_borrow(operation.opcode) && !operation.operands.empty()) {
                    if (const auto source = return_borrow_origins.find(operation.operands[0]); source != return_borrow_origins.end()) {
                        auto origin = source->second;
                        origin.path += " -> " + operation.result + " (" + operation.opcode + ")";
                        if (operation.opcode.starts_with("aggregate.borrow.mut")) origin.mode = BorrowMode::mutable_;
                        else if (operation.opcode.starts_with("aggregate.borrow.")) origin.mode = BorrowMode::immutable;
                        return_borrow_origins.emplace(operation.result, origin);
                        block_return_origin_changed = true;
                    }
                } else if ((operation.opcode == "call" || operation.opcode == "call.indirect") && !operation.operands.empty()) {
                    const bool indirect = operation.opcode == "call.indirect";
                    const std::size_t signature_index = indirect ? 1U : 0U;
                    if (operation.operands.size() <= signature_index || !operation.operands[signature_index].starts_with("@")) continue;
                    const auto callee = function_table.find(operation.operands[signature_index].substr(1));
                    if (callee != function_table.end() && callee->second->return_borrow_mode != BorrowMode::none &&
                        callee->second->return_borrow_parameter >= 0) {
                        const auto argument_index = static_cast<std::size_t>(callee->second->return_borrow_parameter) + (indirect ? 2U : 1U);
                        if (argument_index < operation.operands.size()) {
                            if (const auto source = return_borrow_origins.find(operation.operands[argument_index]); source != return_borrow_origins.end()) {
                                auto origin = source->second;
                                origin.mode = callee->second->return_borrow_mode;
                                origin.path += " -> " + operation.result + (indirect ? " (call.indirect as @" : " (call @") + callee->second->name + ")";
                                return_borrow_origins.emplace(operation.result, origin);
                                block_return_origin_changed = true;
                            }
                        }
                    }
                }
            }
        }

        std::vector<bool> reachable(block_count, false);
        reachable[0] = true;
        bool reach_changed = true;
        while (reach_changed) {
            reach_changed = false;
            for (std::size_t i = 0; i < block_count; ++i) {
                if (!reachable[i]) continue;
                for (const auto& operation : function.blocks[i].operations) {
                    for (const auto& successor : operation.successors) {
                        const auto found = block_index.find(successor);
                        if (found != block_index.end() && !reachable[found->second]) {
                            reachable[found->second] = true;
                            reach_changed = true;
                        }
                    }
                }
            }
        }

        std::vector<std::unordered_set<std::size_t>> dominators(block_count);
        for (std::size_t i = 0; i < block_count; ++i) {
            if (i == 0) {
                dominators[i].insert(0);
            } else if (reachable[i]) {
                for (std::size_t j = 0; j < block_count; ++j) {
                    if (reachable[j]) dominators[i].insert(j);
                }
            } else {
                dominators[i].insert(i);
            }
        }
        bool dom_changed = true;
        while (dom_changed) {
            dom_changed = false;
            for (std::size_t i = 1; i < block_count; ++i) {
                if (!reachable[i] || predecessors[i].empty()) continue;

                // Dominance is defined over paths reachable from the function entry.
                // A syntactically present edge from an unreachable predecessor cannot
                // participate in any execution path to this block. Including that dead
                // predecessor in the intersection incorrectly strips entry (and other
                // real dominators) from reachable blocks.
                std::size_t first_reachable_predecessor = predecessors[i].size();
                for (std::size_t p = 0; p < predecessors[i].size(); ++p) {
                    if (reachable[predecessors[i][p]]) {
                        first_reachable_predecessor = p;
                        break;
                    }
                }
                if (first_reachable_predecessor == predecessors[i].size()) continue;

                std::unordered_set<std::size_t> next = dominators[predecessors[i][first_reachable_predecessor]];
                for (std::size_t p = first_reachable_predecessor + 1; p < predecessors[i].size(); ++p) {
                    if (!reachable[predecessors[i][p]]) continue;
                    std::unordered_set<std::size_t> intersection;
                    for (const auto candidate : next) {
                        if (dominators[predecessors[i][p]].contains(candidate)) intersection.insert(candidate);
                    }
                    next = std::move(intersection);
                }
                next.insert(i);
                if (next != dominators[i]) {
                    dominators[i] = std::move(next);
                    dom_changed = true;
                }
            }
        }

        struct Definition { std::size_t block; std::size_t operation; Type type; AggregateRefKind aggregate_kind{AggregateRefKind::scalar}; std::string aggregate_name; std::string function_signature_name; std::string memory_signature_name; std::uint32_t memory_element_count{}; };
        std::unordered_map<std::string, Definition> definitions;
        std::unordered_map<std::string, std::string> function_address_targets;
        for (const auto& parameter : function.parameters) {
            if (!definitions.emplace(parameter.name, Definition{0, 0, parameter.type, parameter.aggregate_kind, parameter.aggregate_name, parameter.function_signature_name, {}, 0}).second) {
                diagnostics.push_back({DiagnosticSeverity::error, "duplicate SSA value " + parameter.name, {}});
            }
        }
        for (std::size_t block = 0; block < block_count; ++block) {
            for (const auto& parameter : function.blocks[block].parameters) {
                if (!definitions.emplace(parameter.name, Definition{block, 0, parameter.type, parameter.aggregate_kind, parameter.aggregate_name, parameter.function_signature_name, {}, 0}).second) {
                    diagnostics.push_back({DiagnosticSeverity::error, "duplicate SSA value " + parameter.name, {}});
                }
            }
            for (std::size_t operation = 0; operation < function.blocks[block].operations.size(); ++operation) {
                const auto& op = function.blocks[block].operations[operation];
                AggregateRefKind aggregate_kind = AggregateRefKind::scalar;
                std::string aggregate_name;
                if ((op.opcode == "stack.alloc.struct" || op.opcode == "stack.alloc.array") && !op.operands.empty()) {
                    aggregate_kind = op.opcode == "stack.alloc.struct" ? AggregateRefKind::structure : AggregateRefKind::array;
                    aggregate_name = op.operands[0].starts_with("@") ? op.operands[0].substr(1) : op.operands[0];
                } else if ((op.opcode == "aggregate.move.struct" || op.opcode == "aggregate.move.array" ||
                            op.opcode == "aggregate.borrow.struct" || op.opcode == "aggregate.borrow.array" ||
                            op.opcode == "aggregate.borrow.mut.struct" || op.opcode == "aggregate.borrow.mut.array" ||
                            op.opcode == "aggregate.attach.struct" || op.opcode == "aggregate.attach.array") && op.operands.size() == 2) {
                    aggregate_kind = (op.opcode.ends_with("struct")) ? AggregateRefKind::structure : AggregateRefKind::array;
                    aggregate_name = op.operands[1].starts_with("@") ? op.operands[1].substr(1) : op.operands[1];
                } else if (op.opcode == "call" && !op.operands.empty() && op.operands[0].starts_with("@")) {
                    const auto callee = function_table.find(op.operands[0].substr(1));
                    if (callee != function_table.end() && callee->second->returns_aggregate()) {
                        aggregate_kind = callee->second->return_aggregate_kind;
                        aggregate_name = callee->second->return_aggregate_name;
                    }
                } else if (op.opcode == "call.indirect" && op.operands.size() > 1 && op.operands[1].starts_with("@")) {
                    const auto signature = function_table.find(op.operands[1].substr(1));
                    if (signature != function_table.end() && signature->second->returns_aggregate()) {
                        aggregate_kind = signature->second->return_aggregate_kind;
                        aggregate_name = signature->second->return_aggregate_name;
                    }
                } else if ((op.opcode == "global.address" || op.opcode == "tls.address") && !op.operands.empty() && op.operands[0].starts_with("@")) {
                    const auto global = std::find_if(module.globals().begin(), module.globals().end(), [&](const Global& item) {
                        return item.name == op.operands[0].substr(1);
                    });
                    if (global != module.globals().end()) {
                        aggregate_kind = global->aggregate_kind;
                        aggregate_name = global->aggregate_name;
                    }
                } else if (op.opcode == "copy" && !op.operands.empty()) {
                    const auto source = definitions.find(op.operands[0]);
                    if (source != definitions.end()) {
                        aggregate_kind = source->second.aggregate_kind;
                        aggregate_name = source->second.aggregate_name;
                    }
                }
                if ((op.opcode == "func.address" || op.opcode == "callback.address") && !op.result.empty() && !op.operands.empty() && op.operands[0].starts_with("@"))
                    function_address_targets.emplace(op.result, op.operands[0].substr(1));
                std::string function_signature_name;
                std::string memory_signature_name;
                std::uint32_t memory_element_count = 0;
                const auto callback_signature = std::find_if(op.attributes.begin(), op.attributes.end(), [](const Attribute& attribute) {
                    return attribute.name == "callback.signature";
                });
                if (callback_signature != op.attributes.end()) {
                    if (op.opcode == "stack.alloc") {
                        memory_signature_name = callback_signature->value;
                        memory_element_count = 1;
                    } else {
                        function_signature_name = callback_signature->value;
                    }
                }
                if ((op.opcode == "func.address" || op.opcode == "callback.address") && op.operands.size() > 1 && op.operands[1].starts_with("@"))
                    function_signature_name = op.operands[1].substr(1);
                else if (op.opcode == "func.address" && !op.operands.empty() && op.operands[0].starts_with("@"))
                    function_signature_name = op.operands[0].substr(1);
                else if ((op.opcode == "global.address" || op.opcode == "tls.address") && !op.operands.empty() && op.operands[0].starts_with("@")) {
                    const auto global = std::find_if(module.globals().begin(), module.globals().end(), [&](const Global& item) { return item.name == op.operands[0].substr(1); });
                    if (global != module.globals().end()) { memory_signature_name = global->function_signature_name; memory_element_count = global->element_count; }
                } else if (op.opcode == "callback.element.address" && !op.operands.empty()) {
                    const auto base = definitions.find(op.operands[0]);
                    if (base != definitions.end()) { memory_signature_name = base->second.memory_signature_name; memory_element_count = 1; }
                } else if (op.opcode == "load" && !op.operands.empty()) {
                    const auto address = definitions.find(op.operands[0]);
                    if (address != definitions.end()) function_signature_name = address->second.memory_signature_name;
                } else if (op.opcode == "copy" && !op.operands.empty()) {
                    const auto source = definitions.find(op.operands[0]);
                    if (source != definitions.end()) {
                        function_signature_name = source->second.function_signature_name;
                        memory_signature_name = source->second.memory_signature_name;
                        memory_element_count = source->second.memory_element_count;
                    }
                }
                if (!op.result.empty() && !definitions.emplace(op.result, Definition{block, operation + 1,
                        op.opcode.starts_with("cmp.") ? Type(TypeKind::i1) : op.type, aggregate_kind, std::move(aggregate_name), std::move(function_signature_name), std::move(memory_signature_name), memory_element_count}).second) {
                    diagnostics.push_back({DiagnosticSeverity::error, "duplicate SSA value " + op.result, {}});
                }
            }
        }

        for (std::size_t block = 0; block < block_count; ++block) {
            const auto& current = function.blocks[block];
            if (current.operations.empty()) {
                diagnostics.push_back({DiagnosticSeverity::error, "block " + current.name + " is empty", {}});
                continue;
            }

            auto verify_use = [&](const std::string& value, std::size_t operation_index) {
                if (!value.starts_with('%')) return;
                const auto found = definitions.find(value);
                if (found == definitions.end()) {
                    diagnostics.push_back({DiagnosticSeverity::error, "use of undefined value " + value + " in function @" + function.name + " block " + current.name + " operation " + std::to_string(operation_index), {}});
                    return;
                }
                const auto& definition = found->second;
                bool valid = false;
                if (!reachable[block]) valid = true;
                else if (definition.block == block) valid = definition.operation <= operation_index;
                else valid = dominators[block].contains(definition.block);
                if (!valid) {
                    diagnostics.push_back({DiagnosticSeverity::error, "definition of " + value + " does not dominate its use in block " + current.name, {}});
                }
            };

            for (std::size_t operation_index = 0; operation_index < current.operations.size(); ++operation_index) {
                const auto& operation = current.operations[operation_index];
                for (std::size_t operand_index = 0; operand_index < operation.operands.size(); ++operand_index) {
                    if (operation.opcode == "call" && operand_index == 0) continue;
                    if (operation.opcode == "call.indirect" && operand_index == 1 && operation.operands[1].starts_with("@")) continue;
                    verify_use(operation.operands[operand_index], operation_index);
                }
                for (const auto& arguments : operation.successor_arguments) {
                    for (const auto& argument : arguments) verify_use(argument, operation_index);
                }
                if (operation.opcode == "branch" && !operation.operands.empty()) {
                    const auto condition = definitions.find(operation.operands[0]);
                    if (condition != definitions.end() && condition->second.type != Type(TypeKind::i1)) {
                        diagnostics.push_back({DiagnosticSeverity::error,
                            "branch condition must have type i1 in block " + current.name, {}});
                    }
                }
                if (operation.opcode == "store" && operation.operands.size() == 2) {
                    const auto value = definitions.find(operation.operands[0]);
                    const auto address = definitions.find(operation.operands[1]);
                    if (address != definitions.end() && !address->second.memory_signature_name.empty() &&
                        (value == definitions.end() || value->second.function_signature_name != address->second.memory_signature_name))
                        diagnostics.push_back({DiagnosticSeverity::error, "callback store signature mismatch for " + operation.operands[1], {}});
                }
                if (operation.opcode == "global.address") {
                    if ((operation.type != Type(TypeKind::ptr) && operation.type != Type(TypeKind::i64)) || operation.result.empty() ||
                        operation.operands.size() != 1 || !operation.operands[0].starts_with("@")) {
                        diagnostics.push_back({DiagnosticSeverity::error, "global.address requires pointer-sized ptr/i64 result and global symbol", {}});
                    } else if (!global_names.contains(operation.operands[0].substr(1))) {
                        diagnostics.push_back({DiagnosticSeverity::error, "unknown global address " + operation.operands[0], {}});
                    }
                }
                if (operation.opcode == "tls.address") {
                    if (operation.type != Type(TypeKind::ptr) || operation.result.empty() ||
                        operation.operands.size() != 1 || !operation.operands[0].starts_with("@")) {
                        diagnostics.push_back({DiagnosticSeverity::error, "tls.address requires ptr result and TLS global symbol", {}});
                    } else {
                        const auto name = operation.operands[0].substr(1);
                        const auto found = std::find_if(module.globals().begin(), module.globals().end(), [&](const Global& global) { return global.name == name; });
                        if (found == module.globals().end()) diagnostics.push_back({DiagnosticSeverity::error, "unknown TLS address " + operation.operands[0], {}});
                        else if (!found->is_thread_local) diagnostics.push_back({DiagnosticSeverity::error, "tls.address references non-TLS global " + operation.operands[0], {}});
                    }
                }
                if (operation.opcode == "func.address" || operation.opcode == "callback.address") {
                    if (operation.type != Type(TypeKind::ptr) || operation.result.empty() ||
                        operation.operands.empty() || operation.operands.size() > 2 || !operation.operands[0].starts_with("@")) {
                        diagnostics.push_back({DiagnosticSeverity::error, "func.address requires ptr result and function symbol", {}});
                    } else if (operation.opcode == "func.address" && !function_table.contains(operation.operands[0].substr(1))) {
                        diagnostics.push_back({DiagnosticSeverity::error, "unknown function address " + operation.operands[0], {}});
                    } else if (operation.opcode == "callback.address") {
                        if (operation.operands.size() != 2 || !operation.operands[1].starts_with("@"))
                            diagnostics.push_back({DiagnosticSeverity::error, "callback.address requires 'as @signature'", {}});
                        else {
                            const auto signature = function_table.find(operation.operands[1].substr(1));
                            if (signature == function_table.end() || !signature->second->is_signature)
                                diagnostics.push_back({DiagnosticSeverity::error, "unknown callback signature " + operation.operands[1], {}});
                        }
                    }
                }
                if (operation.opcode == "call.indirect") {
                    const auto pointer = operation.operands.empty() ? definitions.end() : definitions.find(operation.operands[0]);
                    if (operation.operands.empty()) {
                        diagnostics.push_back({DiagnosticSeverity::error, "call.indirect requires a function pointer", {}});
                    } else {
                        if (pointer != definitions.end() && pointer->second.type != Type(TypeKind::ptr))
                            diagnostics.push_back({DiagnosticSeverity::error, "call.indirect target must have type ptr", {}});
                    }
                    if (operation.type.kind() == TypeKind::void_ && !operation.result.empty())
                        diagnostics.push_back({DiagnosticSeverity::error, "void call.indirect cannot define a result", {}});
                    if (operation.type.kind() != TypeKind::void_ && operation.result.empty())
                        diagnostics.push_back({DiagnosticSeverity::error, "value-returning call.indirect requires a result", {}});
                    if (operation.operands.size() < 2 || !operation.operands[1].starts_with("@")) {
                        diagnostics.push_back({DiagnosticSeverity::error, "call.indirect requires 'as @signature' for typed indirect calls", {}});
                    } else {
                        const auto signature = function_table.find(operation.operands[1].substr(1));
                        if (signature == function_table.end()) {
                            diagnostics.push_back({DiagnosticSeverity::error, "unknown indirect call signature " + operation.operands[1], {}});
                        } else {
                            const auto& target = *signature->second;
                            if (pointer != definitions.end() && !pointer->second.function_signature_name.empty() &&
                                pointer->second.function_signature_name != target.name)
                                diagnostics.push_back({DiagnosticSeverity::error, "function pointer " + operation.operands[0] +
                                    " has signature @" + pointer->second.function_signature_name + " but indirect call requests " + operation.operands[1], {}});
                            if (const auto addressed = function_address_targets.find(operation.operands[0]); addressed != function_address_targets.end()) {
                                const auto actual = function_table.find(addressed->second);
                                const auto compatible = [&](const Function& left, const Function& right) {
                                    if (left.return_type != right.return_type || left.return_aggregate_kind != right.return_aggregate_kind ||
                                        left.return_aggregate_name != right.return_aggregate_name || left.return_owned != right.return_owned ||
                                        left.return_borrow_mode != right.return_borrow_mode || left.return_borrow_parameter != right.return_borrow_parameter ||
                                        left.variadic != right.variadic || left.calling_convention != right.calling_convention ||
                                        left.parameters.size() != right.parameters.size()) return false;
                                    for (std::size_t index = 0; index < left.parameters.size(); ++index) {
                                        const auto& a = left.parameters[index]; const auto& b = right.parameters[index];
                                        if (a.type != b.type || a.aggregate_kind != b.aggregate_kind || a.aggregate_name != b.aggregate_name ||
                                            a.owned != b.owned || a.borrow_mode != b.borrow_mode) return false;
                                    }
                                    return true;
                                };
                                if (actual != function_table.end() && !compatible(*actual->second, target))
                                    diagnostics.push_back({DiagnosticSeverity::error, "function pointer @" + addressed->second + " is incompatible with indirect signature " + operation.operands[1], {}});
                            }
                            if (operation.type != target.return_type)
                                diagnostics.push_back({DiagnosticSeverity::error, "indirect call return type mismatch for signature " + operation.operands[1], {}});
                            const auto argument_count = operation.operands.size() - 2;
                            const bool count_matches = target.variadic ? argument_count >= target.parameters.size()
                                                                       : argument_count == target.parameters.size();
                            if (!count_matches)
                                diagnostics.push_back({DiagnosticSeverity::error, "indirect call argument count mismatch for signature " + operation.operands[1], {}});
                            const auto count = std::min(argument_count, target.parameters.size());
                            for (std::size_t i = 0; i < count; ++i) {
                                const auto found = definitions.find(operation.operands[i + 2]);
                                if (found == definitions.end()) continue;
                                if (found->second.type != target.parameters[i].type)
                                    diagnostics.push_back({DiagnosticSeverity::error, "indirect call argument type mismatch for signature " + operation.operands[1] + " argument " + std::to_string(i) + ": expected " + target.parameters[i].type.str() + ", got " + found->second.type.str() + " from " + operation.operands[i + 2], {}});
                                else if (target.parameters[i].is_aggregate() &&
                                         (found->second.aggregate_kind != target.parameters[i].aggregate_kind ||
                                          found->second.aggregate_name != target.parameters[i].aggregate_name))
                                    diagnostics.push_back({DiagnosticSeverity::error, "indirect call aggregate argument mismatch for signature " + operation.operands[1], {}});
                                else if (!target.parameters[i].function_signature_name.empty() &&
                                         found->second.function_signature_name != target.parameters[i].function_signature_name)
                                    diagnostics.push_back({DiagnosticSeverity::error, "indirect call callback argument signature mismatch for " + operation.operands[1], {}});
                            }
                        }
                    }
                }
                if (operation.opcode == "call") {
                    if (operation.operands.empty() || !operation.operands[0].starts_with("@")) {
                        diagnostics.push_back({DiagnosticSeverity::error, "call requires a function symbol", {}});
                    } else {
                        const auto callee = function_table.find(operation.operands[0].substr(1));
                        if (callee == function_table.end()) {
                            diagnostics.push_back({DiagnosticSeverity::error, "unknown callee " + operation.operands[0], {}});
                        } else {
                            const auto& target = *callee->second;
                            if (operation.type != target.return_type)
                                diagnostics.push_back({DiagnosticSeverity::error, "call return type mismatch for " + operation.operands[0], {}});
                            if (target.return_type.kind() == TypeKind::void_ && !operation.result.empty())
                                diagnostics.push_back({DiagnosticSeverity::error, "void call cannot define a result for " + operation.operands[0], {}});
                            if (target.return_type.kind() != TypeKind::void_ && operation.result.empty())
                                diagnostics.push_back({DiagnosticSeverity::error, "value-returning call requires a result for " + operation.operands[0], {}});
                            const auto supplied_arguments = operation.operands.size() - 1;
                            const bool count_matches = target.variadic ? supplied_arguments >= target.parameters.size()
                                                                       : supplied_arguments == target.parameters.size();
                            if (!count_matches)
                                diagnostics.push_back({DiagnosticSeverity::error, "call argument count mismatch for " + operation.operands[0], {}});
                            const auto count = std::min(operation.operands.size() - 1, target.parameters.size());
                            for (std::size_t i = 0; i < count; ++i) {
                                const auto found = definitions.find(operation.operands[i + 1]);
                                if (found != definitions.end()) {
                                    if (found->second.type != target.parameters[i].type)
                                        {
                                        std::string producer = "parameter";
                                        if (found->second.operation != 0 && found->second.block < function.blocks.size()) {
                                            const auto& producer_block = function.blocks[found->second.block];
                                            const auto producer_index = found->second.operation - 1;
                                            if (producer_index < producer_block.operations.size()) producer = producer_block.operations[producer_index].opcode;
                                        }
                                        diagnostics.push_back({DiagnosticSeverity::error, "call argument type mismatch in @" + function.name + " block " + current.name + " for " + operation.operands[0] + " argument " + std::to_string(i) + ": expected " + target.parameters[i].type.str() + ", got " + found->second.type.str() + " from " + operation.operands[i + 1] + " produced by " + producer, {}});
                                    }
                                    else if (target.parameters[i].is_aggregate() &&
                                             (found->second.aggregate_kind != target.parameters[i].aggregate_kind ||
                                              found->second.aggregate_name != target.parameters[i].aggregate_name))
                                        diagnostics.push_back({DiagnosticSeverity::error, "call aggregate argument mismatch for " + operation.operands[0], {}});
                                    else if (!target.parameters[i].function_signature_name.empty() &&
                                             found->second.function_signature_name != target.parameters[i].function_signature_name)
                                        diagnostics.push_back({DiagnosticSeverity::error, "call callback argument signature mismatch for " + operation.operands[0], {}});
                                }
                            }
                        }
                    }
                }
                const auto operand_type = [&](std::size_t index) -> Type {
                    if (index >= operation.operands.size()) return Type{};
                    const auto found = definitions.find(operation.operands[index]);
                    return found == definitions.end() ? Type{} : found->second.type;
                };
                if (operation.opcode.starts_with("cmp.")) {
                    const auto left = operand_type(0);
                    const auto right = operand_type(1);
                    const bool float_comparison = operation.type.is_float();
                    const bool supported_float_cmp = operation.opcode == "cmp.eq" || operation.opcode == "cmp.ne" ||
                        operation.opcode == "cmp.lt" || operation.opcode == "cmp.le" || operation.opcode == "cmp.gt" || operation.opcode == "cmp.ge";
                    if (left != operation.type || right != operation.type ||
                        (!operation.type.is_integer() && !(float_comparison && supported_float_cmp))) {
                        diagnostics.push_back({DiagnosticSeverity::error,
                            "comparison requires matching supported numeric operands in block " + current.name, {}});
                    }
                }
                if (operation.opcode == "select") {
                    const auto condition = operand_type(0);
                    const auto when_true = operand_type(1);
                    const auto when_false = operand_type(2);
                    if (operation.operands.size() != 3 || condition != Type(TypeKind::i1) ||
                        when_true != operation.type || when_false != operation.type ||
                        (!operation.type.is_integer() && operation.type != Type(TypeKind::ptr))) {
                        diagnostics.push_back({DiagnosticSeverity::error,
                            "select requires i1 condition and matching integer operands in block " + current.name, {}});
                    }
                }
                if (operation.opcode == "add" || operation.opcode == "sub" || operation.opcode == "mul" || operation.opcode == "div") {
                    const auto left = operand_type(0);
                    const auto right = operand_type(1);
                    if (left != operation.type || right != operation.type || !operation.type.is_numeric() ||
                        (operation.opcode == "div" && !operation.type.is_float())) {
                        diagnostics.push_back({DiagnosticSeverity::error,
                            operation.opcode + " requires matching numeric operands in block " + current.name, {}});
                    }
                }
                if (operation.opcode == "neg" && operand_type(0) != operation.type) {
                    diagnostics.push_back({DiagnosticSeverity::error,
                        "neg requires a matching operand type in block " + current.name, {}});
                }
                if (operation.opcode == "bitcast") {
                    const auto source = operand_type(0);
                    const bool valid = operation.operands.size() == 1 &&
                        ((source == Type(TypeKind::ptr) && operation.type == Type(TypeKind::i64)) ||
                         (source == Type(TypeKind::i64) && operation.type == Type(TypeKind::ptr)) ||
                         (source == Type(TypeKind::ptr) && operation.type == Type(TypeKind::ptr)));
                    if (!valid) diagnostics.push_back({DiagnosticSeverity::error,
                        "bitcast requires ptr/i64-compatible source and result in block " + current.name, {}});
                }
                if (operation.opcode == "zero_extend" || operation.opcode == "sign_extend" || operation.opcode == "truncate") {
                    const auto source = operand_type(0);
                    if (operation.operands.size() != 1 || !source.is_integer() || !operation.type.is_integer()) {
                        diagnostics.push_back({DiagnosticSeverity::error,
                            operation.opcode + " requires one integer operand and integer result in block " + current.name, {}});
                    } else {
                        const auto width = [](Type type) -> unsigned {
                            switch (type.kind()) {
                            case TypeKind::i1: return 1;
                            case TypeKind::i8: return 8;
                            case TypeKind::i16: return 16;
                            case TypeKind::i32: return 32;
                            case TypeKind::i64: return 64;
                            default: return 0;
                            }
                        };
                        const auto source_width = width(source);
                        const auto result_width = width(operation.type);
                        const bool extension = operation.opcode == "zero_extend" || operation.opcode == "sign_extend";
                        if ((extension && source_width >= result_width) || (!extension && source_width <= result_width)) {
                            diagnostics.push_back({DiagnosticSeverity::error,
                                operation.opcode + " has invalid integer widths in block " + current.name, {}});
                        }
                    }
                }
                if (operation.opcode == "int_to_float.signed" || operation.opcode == "int_to_float.unsigned" ||
                    operation.opcode == "float_to_int.signed" || operation.opcode == "float_to_int.unsigned" ||
                    operation.opcode == "float_extend" || operation.opcode == "float_truncate") {
                    const auto source = operand_type(0);
                    const bool i2f = operation.opcode == "int_to_float.signed" || operation.opcode == "int_to_float.unsigned";
                    const bool f2i = operation.opcode == "float_to_int.signed" || operation.opcode == "float_to_int.unsigned";
                    const bool f2f = operation.opcode == "float_extend" || operation.opcode == "float_truncate";
                    bool valid = operation.operands.size() == 1;
                    if (i2f) valid = valid && source.is_integer() && operation.type.is_float();
                    if (f2i) valid = valid && source.is_float() && operation.type.is_integer();
                    if (f2f) valid = valid && source.is_float() && operation.type.is_float() && source != operation.type;
                    if (!valid) diagnostics.push_back({DiagnosticSeverity::error, operation.opcode +
                        " has incompatible source/result types in block " + current.name, {}});
                }
                if (operation.alignment != 0) {
                    const bool allowed = operation.opcode == "stack.alloc" || operation.opcode == "stack.alloc.struct" ||
                                         operation.opcode == "stack.alloc.array" || operation.opcode == "load" || operation.opcode == "store";
                    const bool power_of_two = (operation.alignment & (operation.alignment - 1U)) == 0;
                    if (!allowed) {
                        diagnostics.push_back({DiagnosticSeverity::error,
                            "alignment is only valid on stack allocation, load, or store in block " + current.name, {}});
                    } else if (!power_of_two || operation.alignment > 4096U) {
                        diagnostics.push_back({DiagnosticSeverity::error,
                            "alignment must be a power of two no greater than 16 in block " + current.name, {}});
                    }
                }
                if (operation.opcode == "stack.alloc" && operation.type != Type(TypeKind::ptr)) {
                    diagnostics.push_back({DiagnosticSeverity::error,
                        "stack.alloc must produce ptr in block " + current.name, {}});
                }
                if (operation.opcode == "stack.alloc.struct" || operation.opcode == "stack.alloc.array") {
                    const bool structure = operation.opcode == "stack.alloc.struct";
                    const bool known = operation.operands.size() == 1 && operation.operands[0].starts_with("@") &&
                        (structure ? struct_table.contains(operation.operands[0].substr(1)) : array_table.contains(operation.operands[0].substr(1)));
                    if (operation.type != Type(TypeKind::ptr) || operation.result.empty() || !known) {
                        diagnostics.push_back({DiagnosticSeverity::error,
                            operation.opcode + " requires a known aggregate and ptr result in block " + current.name, {}});
                    }
                }
                if (operation.opcode == "aggregate.zero.struct" || operation.opcode == "aggregate.zero.array") {
                    const bool structure = operation.opcode == "aggregate.zero.struct";
                    const bool known = operation.operands.size() == 2 && operation.operands[1].starts_with("@") &&
                        (structure ? struct_table.contains(operation.operands[1].substr(1)) : array_table.contains(operation.operands[1].substr(1)));
                    const bool valid = operation.type == Type(TypeKind::void_) && operation.result.empty() && known &&
                        operand_type(0) == Type(TypeKind::ptr);
                    if (!valid) diagnostics.push_back({DiagnosticSeverity::error,
                        operation.opcode + " requires destination ptr, known aggregate, void type, and no result in block " + current.name, {}});
                }
                if (operation.opcode == "aggregate.copy.struct" || operation.opcode == "aggregate.copy.array") {
                    const bool structure = operation.opcode == "aggregate.copy.struct";
                    const bool known = operation.operands.size() == 3 && operation.operands[2].starts_with("@") &&
                        (structure ? struct_table.contains(operation.operands[2].substr(1)) : array_table.contains(operation.operands[2].substr(1)));
                    const bool valid = operation.type == Type(TypeKind::void_) && operation.result.empty() && known &&
                        operand_type(0) == Type(TypeKind::ptr) && operand_type(1) == Type(TypeKind::ptr);
                    if (!valid) diagnostics.push_back({DiagnosticSeverity::error,
                        operation.opcode + " requires destination ptr, source ptr, known aggregate, void type, and no result in block " + current.name, {}});
                }
                if (operation.opcode == "aggregate.move.struct" || operation.opcode == "aggregate.move.array") {
                    const bool structure = operation.opcode == "aggregate.move.struct";
                    const bool known = operation.operands.size() == 2 && operation.operands[1].starts_with("@") &&
                        (structure ? struct_table.contains(operation.operands[1].substr(1)) : array_table.contains(operation.operands[1].substr(1)));
                    const auto source = operation.operands.empty() ? definitions.end() : definitions.find(operation.operands[0]);
                    const bool identity = source != definitions.end() && source->second.aggregate_kind ==
                        (structure ? AggregateRefKind::structure : AggregateRefKind::array) &&
                        source->second.aggregate_name == operation.operands[1].substr(1);
                    const bool valid = operation.type == Type(TypeKind::ptr) && !operation.result.empty() && known && identity;
                    if (!valid) diagnostics.push_back({DiagnosticSeverity::error,
                        operation.opcode + " requires a matching named aggregate source and ptr result in block " + current.name, {}});
                }
                if (operation.opcode == "aggregate.attach.struct" || operation.opcode == "aggregate.attach.array") {
                    const bool structure = operation.opcode.ends_with("struct");
                    const bool known = operation.operands.size() == 2 && operation.operands[1].starts_with("@") &&
                        (structure ? struct_table.contains(operation.operands[1].substr(1)) : array_table.contains(operation.operands[1].substr(1)));
                    const bool valid = operation.type == Type(TypeKind::ptr) && !operation.result.empty() && known &&
                        operand_type(0) == Type(TypeKind::ptr);
                    if (!valid) diagnostics.push_back({DiagnosticSeverity::error,
                        operation.opcode + " requires a ptr source, known aggregate, and ptr result in block " + current.name, {}});
                }
                if (operation.opcode == "aggregate.borrow.struct" || operation.opcode == "aggregate.borrow.array" ||
                    operation.opcode == "aggregate.borrow.mut.struct" || operation.opcode == "aggregate.borrow.mut.array") {
                    const bool structure = operation.opcode.ends_with("struct");
                    const bool known = operation.operands.size() == 2 && operation.operands[1].starts_with("@") &&
                        (structure ? struct_table.contains(operation.operands[1].substr(1)) : array_table.contains(operation.operands[1].substr(1)));
                    const auto source = operation.operands.empty() ? definitions.end() : definitions.find(operation.operands[0]);
                    const bool identity = source != definitions.end() && source->second.aggregate_kind ==
                        (structure ? AggregateRefKind::structure : AggregateRefKind::array) &&
                        source->second.aggregate_name == operation.operands[1].substr(1);
                    const bool valid = operation.type == Type(TypeKind::ptr) && !operation.result.empty() && known && identity;
                    if (!valid) diagnostics.push_back({DiagnosticSeverity::error,
                        operation.opcode + " requires a matching named aggregate source and ptr result in block " + current.name, {}});
                }
                if (operation.opcode == "aggregate.borrow.end.struct" || operation.opcode == "aggregate.borrow.end.array") {
                    const bool structure = operation.opcode.ends_with("struct");
                    const bool known = operation.operands.size() == 2 && operation.operands[1].starts_with("@") &&
                        (structure ? struct_table.contains(operation.operands[1].substr(1)) : array_table.contains(operation.operands[1].substr(1)));
                    const auto source = operation.operands.empty() ? definitions.end() : definitions.find(operation.operands[0]);
                    const bool identity = source != definitions.end() && source->second.aggregate_kind ==
                        (structure ? AggregateRefKind::structure : AggregateRefKind::array) &&
                        source->second.aggregate_name == operation.operands[1].substr(1);
                    const bool valid = operation.type == Type(TypeKind::void_) && operation.result.empty() && known && identity;
                    if (!valid) diagnostics.push_back({DiagnosticSeverity::error,
                        operation.opcode + " requires a matching aggregate borrow, void type, and no result in block " + current.name, {}});
                }
                if (operation.opcode == "aggregate.end.struct" || operation.opcode == "aggregate.end.array") {
                    const bool structure = operation.opcode == "aggregate.end.struct";
                    const bool known = operation.operands.size() == 2 && operation.operands[1].starts_with("@") &&
                        (structure ? struct_table.contains(operation.operands[1].substr(1)) : array_table.contains(operation.operands[1].substr(1)));
                    const auto source = operation.operands.empty() ? definitions.end() : definitions.find(operation.operands[0]);
                    const bool identity = source != definitions.end() && source->second.aggregate_kind ==
                        (structure ? AggregateRefKind::structure : AggregateRefKind::array) &&
                        source->second.aggregate_name == operation.operands[1].substr(1);
                    const bool valid = operation.type == Type(TypeKind::void_) && operation.result.empty() && known && identity;
                    if (!valid) diagnostics.push_back({DiagnosticSeverity::error,
                        operation.opcode + " requires a matching named aggregate source, void type, and no result in block " + current.name, {}});
                }
                if ((operation.opcode == "ptr.offset" || operation.opcode == "field.address") &&
                    (operation.type != Type(TypeKind::ptr) || operation.operands.size() != 2 ||
                     operand_type(0) != Type(TypeKind::ptr))) {
                    diagnostics.push_back({DiagnosticSeverity::error,
                        operation.opcode + " requires a ptr base, byte offset, and ptr result in block " + current.name, {}});
                }
                if (operation.opcode == "struct.field.address") {
                    bool valid = operation.type == Type(TypeKind::ptr) && operation.operands.size() == 3 &&
                                 operand_type(0) == Type(TypeKind::ptr) && operation.operands[1].starts_with("@");
                    const StructDecl* declaration = nullptr;
                    if (valid) {
                        const auto found = struct_table.find(operation.operands[1].substr(1));
                        if (found == struct_table.end()) valid = false;
                        else declaration = found->second;
                    }
                    std::uint32_t field_index{};
                    if (valid) {
                        const auto& text = operation.operands[2];
                        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), field_index);
                        valid = error == std::errc{} && end == text.data() + text.size() && field_index < declaration->fields.size();
                    }
                    if (!valid) diagnostics.push_back({DiagnosticSeverity::error,
                        "struct.field.address requires a ptr base, known structure, valid field index, and ptr result in block " + current.name, {}});
                }
                if (operation.opcode == "struct.field.name.address") {
                    bool valid = operation.type == Type(TypeKind::ptr) && operation.operands.size() == 3 &&
                                 operand_type(0) == Type(TypeKind::ptr) && operation.operands[1].starts_with("@");
                    const StructDecl* declaration = nullptr;
                    if (valid) {
                        const auto found = struct_table.find(operation.operands[1].substr(1));
                        if (found == struct_table.end()) valid = false;
                        else declaration = found->second;
                    }
                    if (valid) {
                        valid = std::any_of(declaration->fields.begin(), declaration->fields.end(), [&](const StructField& field) {
                            return field.name == operation.operands[2];
                        });
                    }
                    if (!valid) diagnostics.push_back({DiagnosticSeverity::error,
                        "struct.field.name.address requires a ptr base, known structure, known field name, and ptr result in block " + current.name, {}});
                }

                if (operation.opcode == "callback.element.address") {
                    bool valid = operation.type == Type(TypeKind::ptr) && operation.operands.size() == 3 &&
                                 operand_type(0) == Type(TypeKind::ptr) && operation.operands[1].starts_with("@");
                    if (valid) {
                        const auto signature_name = operation.operands[1].substr(1);
                        const auto signature = function_table.find(signature_name);
                        const auto base = definitions.find(operation.operands[0]);
                        valid = signature != function_table.end() && signature->second->is_signature &&
                                base != definitions.end() && base->second.memory_signature_name == signature_name;
                        if (valid) {
                            const auto& index_text = operation.operands[2];
                            if (index_text.starts_with('%')) {
                                const auto index_value = definitions.find(index_text);
                                valid = index_value != definitions.end() && index_value->second.type == Type(TypeKind::i64);
                            } else {
                                std::uint32_t index{};
                                const auto [end, error] = std::from_chars(index_text.data(), index_text.data() + index_text.size(), index);
                                valid = error == std::errc{} && end == index_text.data() + index_text.size() &&
                                        (base->second.memory_element_count == 0 || index < base->second.memory_element_count);
                            }
                        }
                    }
                    if (!valid) diagnostics.push_back({DiagnosticSeverity::error,
                        "callback.element.address requires callback storage, matching signature, i64 or in-range constant index, and ptr result in block " + current.name, {}});
                }
                if (operation.opcode == "array.element.address") {
                    bool valid = operation.type == Type(TypeKind::ptr) && operation.operands.size() == 3 &&
                                 operand_type(0) == Type(TypeKind::ptr) && operation.operands[1].starts_with("@");
                    const ArrayDecl* declaration = nullptr;
                    if (valid) {
                        const auto found = array_table.find(operation.operands[1].substr(1));
                        if (found == array_table.end()) valid = false;
                        else declaration = found->second;
                    }
                    if (valid) {
                        const auto& text = operation.operands[2];
                        if (text.starts_with('%')) {
                            const auto definition = definitions.find(text);
                            valid = definition != definitions.end() && definition->second.type == Type(TypeKind::i64);
                        } else {
                            std::uint32_t index{};
                            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), index);
                            valid = error == std::errc{} && end == text.data() + text.size() && index < declaration->element_count;
                        }
                    }
                    if (!valid) diagnostics.push_back({DiagnosticSeverity::error,
                        "array.element.address requires a ptr base, known array, i64 or in-range constant element index, and ptr result in block " + current.name, {}});
                }
                if (operation.opcode == "sizeof.array" || operation.opcode == "alignof.array") {
                    const bool valid = operation.type == Type(TypeKind::i64) && operation.operands.size() == 1 &&
                                       operation.operands[0].starts_with("@") &&
                                       array_table.contains(operation.operands[0].substr(1));
                    if (!valid) diagnostics.push_back({DiagnosticSeverity::error,
                        operation.opcode + " requires a known array and i64 result in block " + current.name, {}});
                }
                if (operation.opcode == "sizeof.struct" || operation.opcode == "alignof.struct") {
                    const bool valid = operation.type == Type(TypeKind::i64) && operation.operands.size() == 1 &&
                                       operation.operands[0].starts_with("@") &&
                                       struct_table.contains(operation.operands[0].substr(1));
                    if (!valid) diagnostics.push_back({DiagnosticSeverity::error,
                        operation.opcode + " requires a known structure and i64 result in block " + current.name, {}});
                }
                if (operation.opcode == "load" && operand_type(0) != Type(TypeKind::ptr)) {
                    diagnostics.push_back({DiagnosticSeverity::error,
                        "load address must have type ptr in block " + current.name, {}});
                }
                if (operation.opcode == "store" &&
                    (operand_type(0) != operation.type || operand_type(1) != Type(TypeKind::ptr))) {
                    diagnostics.push_back({DiagnosticSeverity::error,
                        "store requires a matching value type and ptr address in block " + current.name, {}});
                }
                if (operation.opcode == "memory.copy") {
                    const bool valid = operation.type == Type(TypeKind::void_) && operation.result.empty() &&
                                       operation.operands.size() == 3 &&
                                       operand_type(0) == Type(TypeKind::ptr) &&
                                       operand_type(1) == Type(TypeKind::ptr) &&
                                       operand_type(2) == Type(TypeKind::i64);
                    if (!valid) diagnostics.push_back({DiagnosticSeverity::error,
                        "memory.copy requires destination ptr, source ptr, i64 byte count, void type, and no result in block " + current.name, {}});
                }
                if (operation.opcode == "memory.set") {
                    const bool valid = operation.type == Type(TypeKind::void_) && operation.result.empty() &&
                                       operation.operands.size() == 3 &&
                                       operand_type(0) == Type(TypeKind::ptr) &&
                                       operand_type(1) == Type(TypeKind::i8) &&
                                       operand_type(2) == Type(TypeKind::i64);
                    if (!valid) diagnostics.push_back({DiagnosticSeverity::error,
                        "memory.set requires destination ptr, i8 value, i64 byte count, void type, and no result in block " + current.name, {}});
                }
                if (operation.is_terminator() && operation_index + 1 != current.operations.size()) {
                    diagnostics.push_back({DiagnosticSeverity::error, "terminator is not last in block " + current.name, {}});
                }
                for (std::size_t successor_index = 0; successor_index < operation.successors.size(); ++successor_index) {
                    const auto found = block_index.find(operation.successors[successor_index]);
                    if (found == block_index.end()) {
                        diagnostics.push_back({DiagnosticSeverity::error, "unknown successor " + operation.successors[successor_index], {}});
                        continue;
                    }
                    const auto& parameters = function.blocks[found->second].parameters;
                    const auto& arguments = operation.successor_arguments[successor_index];
                    if (parameters.size() != arguments.size()) {
                        diagnostics.push_back({DiagnosticSeverity::error,
                            "successor " + operation.successors[successor_index] + " expects " + std::to_string(parameters.size()) +
                            " arguments but received " + std::to_string(arguments.size()), {}});
                    }
                    const auto count = std::min(parameters.size(), arguments.size());
                    for (std::size_t i = 0; i < count; ++i) {
                        const auto definition = definitions.find(arguments[i]);
                        if (definition != definitions.end()) {
                            if (definition->second.type != parameters[i].type) {
                                diagnostics.push_back({DiagnosticSeverity::error, "type mismatch for block argument " + arguments[i], {}});
                            } else if (parameters[i].is_aggregate() &&
                                       (definition->second.aggregate_kind != parameters[i].aggregate_kind ||
                                        definition->second.aggregate_name != parameters[i].aggregate_name)) {
                                diagnostics.push_back({DiagnosticSeverity::error, "aggregate identity mismatch for block argument " + arguments[i], {}});
                            } else if (!parameters[i].function_signature_name.empty() &&
                                       definition->second.function_signature_name != parameters[i].function_signature_name) {
                                diagnostics.push_back({DiagnosticSeverity::error, "callback signature mismatch for block argument " + arguments[i], {}});
                            }
                        }
                    }
                }
            }

            if (!current.operations.back().is_terminator()) {
                diagnostics.push_back({DiagnosticSeverity::error, "block " + current.name + " has no terminator", {}});
            }
            if (current.operations.back().opcode == "return") {
                const bool requires_value = function.return_type.kind() != TypeKind::void_;
                const bool has_value = !current.operations.back().operands.empty();
                if (requires_value != has_value) {
                    diagnostics.push_back({DiagnosticSeverity::error, "return value mismatch in @" + function.name, {}});
                } else if (has_value) {
                    const auto found = definitions.find(current.operations.back().operands[0]);
                    if (found != definitions.end() && found->second.type != function.return_type) {
                        diagnostics.push_back({DiagnosticSeverity::error, "return type mismatch in @" + function.name, {}});
                    } else if (function.returns_aggregate() && found != definitions.end() && found->second.type != Type(TypeKind::ptr)) {
                        diagnostics.push_back({DiagnosticSeverity::error, "aggregate return in @" + function.name + " must return a pointer", {}});
                    }
                    if (function.return_borrow_mode != BorrowMode::none) {
                        const auto origin = return_borrow_origins.find(current.operations.back().operands[0]);
                        if (origin == return_borrow_origins.end() || origin->second.parameter_index != static_cast<std::size_t>(function.return_borrow_parameter)) {
                            const auto actual = origin == return_borrow_origins.end()
                                ? std::string("untracked value ") + current.operations.back().operands[0]
                                : std::string("parameter ") + std::to_string(origin->second.parameter_index) + " via " + origin->second.path;
                            diagnostics.push_back({DiagnosticSeverity::error,
                                "borrowed return from @" + function.name + " must derive from parameter " +
                                std::to_string(function.return_borrow_parameter) + ", but resolved " + actual, {}});
                        } else if (function.return_borrow_mode == BorrowMode::mutable_ && origin->second.mode != BorrowMode::mutable_) {
                            diagnostics.push_back({DiagnosticSeverity::error, "mutable borrowed return from @" + function.name + " lost mutable provenance", {}});
                        }
                    }
                }
            }
        }

        // Conservative ownership dataflow: a moved aggregate is unavailable on every path
        // reached after the move. Path joins use union semantics, so a value moved on any
        // incoming path cannot be used after the join without an explicit replacement value.
        std::vector<std::unordered_set<std::string>> moved_in(block_count), moved_out(block_count);
        bool ownership_changed = true;
        while (ownership_changed) {
            ownership_changed = false;
            for (std::size_t block = 0; block < block_count; ++block) {
                std::unordered_set<std::string> incoming;
                for (const auto predecessor : predecessors[block])
                    incoming.insert(moved_out[predecessor].begin(), moved_out[predecessor].end());
                auto outgoing = incoming;
                for (const auto& operation : function.blocks[block].operations) {
                    if ((operation.opcode == "aggregate.move.struct" || operation.opcode == "aggregate.move.array" ||
                         operation.opcode == "aggregate.borrow.end.struct" || operation.opcode == "aggregate.borrow.end.array" ||
                         operation.opcode == "aggregate.end.struct" || operation.opcode == "aggregate.end.array") &&
                        !operation.operands.empty() && operation.operands[0].starts_with('%'))
                        outgoing.insert(operation.operands[0]);
                }
                if (incoming != moved_in[block] || outgoing != moved_out[block]) {
                    moved_in[block] = std::move(incoming);
                    moved_out[block] = std::move(outgoing);
                    ownership_changed = true;
                }
            }
        }
        for (std::size_t block = 0; block < block_count; ++block) {
            auto moved = moved_in[block];
            const auto& current = function.blocks[block];
            for (const auto& operation : current.operations) {
                for (std::size_t index = 0; index < operation.operands.size(); ++index) {
                    if (operation.opcode == "call" && index == 0) continue;
                    const auto& operand = operation.operands[index];
                    if (operand.starts_with('%') && moved.contains(operand))
                        diagnostics.push_back({DiagnosticSeverity::error, "use of moved aggregate " + operand + " in block " + current.name, {}});
                }
                for (const auto& arguments : operation.successor_arguments)
                    for (const auto& argument : arguments)
                        if (argument.starts_with('%') && moved.contains(argument))
                            diagnostics.push_back({DiagnosticSeverity::error, "use of moved aggregate " + argument + " on control-flow edge from block " + current.name, {}});
                if ((operation.opcode == "aggregate.move.struct" || operation.opcode == "aggregate.move.array" ||
                     operation.opcode == "aggregate.borrow.end.struct" || operation.opcode == "aggregate.borrow.end.array" ||
                         operation.opcode == "aggregate.end.struct" || operation.opcode == "aggregate.end.array") &&
                    !operation.operands.empty() && operation.operands[0].starts_with('%'))
                    moved.insert(operation.operands[0]);
            }
        }

        // Conservative borrow analysis. Active borrows are unioned at joins. Borrow
        // provenance is propagated through pointer-addressing operations and compatible
        // block parameters so immutable loans cannot be used as mutation destinations.
        struct BorrowInfo { std::string root; bool mutable_borrow{}; std::string parent_borrow; };
        struct BorrowProvenance { std::string borrow; bool mutable_borrow{}; };
        std::unordered_map<std::string, BorrowInfo> borrow_info;
        std::unordered_map<std::string, BorrowProvenance> provenance;
        std::unordered_set<std::string> entry_borrows;
        for (const auto& parameter : function.parameters) {
            if (parameter.borrow_mode == BorrowMode::none) continue;
            const bool mutable_borrow = parameter.borrow_mode == BorrowMode::mutable_;
            borrow_info.emplace(parameter.name, BorrowInfo{parameter.name, mutable_borrow, {}});
            provenance.emplace(parameter.name, BorrowProvenance{parameter.name, mutable_borrow});
            entry_borrows.insert(parameter.name);
        }
        for (const auto& block : function.blocks) {
            for (const auto& operation : block.operations) {
                if ((operation.opcode == "aggregate.borrow.struct" || operation.opcode == "aggregate.borrow.array" ||
                     operation.opcode == "aggregate.borrow.mut.struct" || operation.opcode == "aggregate.borrow.mut.array") &&
                    !operation.result.empty() && !operation.operands.empty()) {
                    const bool mutable_borrow = operation.opcode.starts_with("aggregate.borrow.mut");
                    std::string root = operation.operands[0];
                    std::string parent;
                    if (const auto source = provenance.find(operation.operands[0]); source != provenance.end()) {
                        parent = source->second.borrow;
                        if (const auto parent_info = borrow_info.find(parent); parent_info != borrow_info.end()) root = parent_info->second.root;
                    }
                    borrow_info.emplace(operation.result, BorrowInfo{root, mutable_borrow, parent});
                    provenance.emplace(operation.result, BorrowProvenance{operation.result, mutable_borrow});
                } else if ((operation.opcode == "call" || operation.opcode == "call.indirect") && !operation.result.empty() && !operation.operands.empty()) {
                    const bool indirect = operation.opcode == "call.indirect";
                    const std::size_t signature_index = indirect ? 1U : 0U;
                    if (operation.operands.size() <= signature_index || !operation.operands[signature_index].starts_with("@")) continue;
                    const auto callee = function_table.find(operation.operands[signature_index].substr(1));
                    if (callee != function_table.end() && callee->second->return_borrow_mode != BorrowMode::none &&
                        callee->second->return_borrow_parameter >= 0) {
                        const auto argument_index = static_cast<std::size_t>(callee->second->return_borrow_parameter) + (indirect ? 2U : 1U);
                        if (argument_index < operation.operands.size()) {
                            const auto& argument = operation.operands[argument_index];
                            std::string root = argument;
                            std::string parent;
                            if (const auto source = provenance.find(argument); source != provenance.end()) {
                                parent = source->second.borrow;
                                if (const auto parent_info = borrow_info.find(parent); parent_info != borrow_info.end()) root = parent_info->second.root;
                            }
                            const bool mutable_borrow = callee->second->return_borrow_mode == BorrowMode::mutable_;
                            borrow_info.emplace(operation.result, BorrowInfo{root, mutable_borrow, parent});
                            provenance.emplace(operation.result, BorrowProvenance{operation.result, mutable_borrow});
                        }
                    }
                }
            }
        }

        const auto propagates_pointer_provenance = [](const std::string& opcode) {
            return opcode == "ptr.offset" || opcode == "field.address" ||
                   opcode == "struct.field.address" || opcode == "struct.field.name.address" ||
                   opcode == "array.element.address" ||
                   opcode == "callback.element.address";
        };

        // Resolve derived addresses and block parameters to their originating borrow.
        bool provenance_changed = true;
        while (provenance_changed) {
            provenance_changed = false;
            for (std::size_t block = 0; block < block_count; ++block) {
                const auto& current = function.blocks[block];
                for (const auto& operation : current.operations) {
                    if (!operation.result.empty() && propagates_pointer_provenance(operation.opcode) && !operation.operands.empty()) {
                        const auto source = provenance.find(operation.operands[0]);
                        if (source != provenance.end() && !provenance.contains(operation.result)) {
                            provenance.emplace(operation.result, source->second);
                            provenance_changed = true;
                        }
                    }
                }
                for (std::size_t parameter_index = 0; parameter_index < current.parameters.size(); ++parameter_index) {
                    const auto& parameter = current.parameters[parameter_index];
                    if (provenance.contains(parameter.name) || predecessors[block].empty()) continue;
                    std::optional<BorrowProvenance> candidate;
                    bool compatible = true;
                    for (const auto predecessor : predecessors[block]) {
                        const auto& terminator = function.blocks[predecessor].operations.back();
                        bool found_edge = false;
                        for (std::size_t edge = 0; edge < terminator.successors.size(); ++edge) {
                            if (terminator.successors[edge] != current.name || edge >= terminator.successor_arguments.size() ||
                                parameter_index >= terminator.successor_arguments[edge].size()) continue;
                            found_edge = true;
                            const auto incoming = provenance.find(terminator.successor_arguments[edge][parameter_index]);
                            if (incoming == provenance.end()) { compatible = false; break; }
                            if (!candidate) candidate = incoming->second;
                            else if (candidate->borrow != incoming->second.borrow || candidate->mutable_borrow != incoming->second.mutable_borrow) {
                                compatible = false; break;
                            }
                        }
                        if (!found_edge || !compatible) { compatible = false; break; }
                    }
                    if (compatible && candidate) {
                        provenance.emplace(parameter.name, *candidate);
                        provenance_changed = true;
                    }
                }
            }
        }

        std::vector<std::unordered_set<std::string>> borrows_in(block_count), borrows_out(block_count);
        bool borrow_changed = true;
        while (borrow_changed) {
            borrow_changed = false;
            for (std::size_t block = 0; block < block_count; ++block) {
                std::unordered_set<std::string> incoming;
                if (block == 0) incoming.insert(entry_borrows.begin(), entry_borrows.end());
                for (const auto predecessor : predecessors[block])
                    incoming.insert(borrows_out[predecessor].begin(), borrows_out[predecessor].end());
                auto outgoing = incoming;
                for (const auto& operation : function.blocks[block].operations) {
                    if (const auto created = borrow_info.find(operation.result); created != borrow_info.end()) {
                        if (created->second.mutable_borrow && !created->second.parent_borrow.empty()) outgoing.erase(created->second.parent_borrow);
                        outgoing.insert(operation.result);
                    }
                    if ((operation.opcode == "aggregate.borrow.end.struct" || operation.opcode == "aggregate.borrow.end.array") &&
                        !operation.operands.empty()) {
                        const auto ended = borrow_info.find(operation.operands[0]);
                        outgoing.erase(operation.operands[0]);
                        if (ended != borrow_info.end() && ended->second.mutable_borrow && !ended->second.parent_borrow.empty())
                            outgoing.insert(ended->second.parent_borrow);
                    }
                }
                if (incoming != borrows_in[block] || outgoing != borrows_out[block]) {
                    borrows_in[block] = std::move(incoming);
                    borrows_out[block] = std::move(outgoing);
                    borrow_changed = true;
                }
            }
        }

        const auto mutation_destination = [](const Operation& operation) -> std::optional<std::size_t> {
            if (operation.opcode == "store") return 1;
            if (operation.opcode == "memory.set" || operation.opcode == "memory.copy" ||
                operation.opcode == "aggregate.zero.struct" || operation.opcode == "aggregate.zero.array" ||
                operation.opcode == "aggregate.copy.struct" || operation.opcode == "aggregate.copy.array") return 0;
            return std::nullopt;
        };

        for (std::size_t block = 0; block < block_count; ++block) {
            auto active = borrows_in[block];
            const auto& current = function.blocks[block];
            for (const auto& operation : current.operations) {
                // Derived addresses are only valid while their originating loan remains active.
                for (std::size_t index = 0; index < operation.operands.size(); ++index) {
                    if ((operation.opcode == "aggregate.borrow.end.struct" || operation.opcode == "aggregate.borrow.end.array") && index == 0)
                        continue;
                    const auto derived = provenance.find(operation.operands[index]);
                    if (derived != provenance.end() && !active.contains(derived->second.borrow)) {
                        diagnostics.push_back({DiagnosticSeverity::error,
                            "use of pointer derived from inactive aggregate borrow " + derived->second.borrow + " in block " + current.name, {}});
                    }
                }

                if (const auto destination_index = mutation_destination(operation);
                    destination_index && *destination_index < operation.operands.size()) {
                    const auto destination = provenance.find(operation.operands[*destination_index]);
                    if (destination != provenance.end() && !destination->second.mutable_borrow) {
                        diagnostics.push_back({DiagnosticSeverity::error,
                            "cannot mutate through immutable aggregate borrow " + destination->second.borrow + " in block " + current.name, {}});
                    }
                }

                if ((operation.opcode == "call" || operation.opcode == "call.indirect") && !operation.operands.empty()) {
                    const bool indirect = operation.opcode == "call.indirect";
                    const std::size_t signature_index = indirect ? 1U : 0U;
                    if (operation.operands.size() <= signature_index || !operation.operands[signature_index].starts_with("@")) continue;
                    const auto callee = function_table.find(operation.operands[signature_index].substr(1));
                    if (callee != function_table.end()) {
                        const std::size_t first_argument = indirect ? 2U : 1U;
                        const auto count = std::min(callee->second->parameters.size(), operation.operands.size() - first_argument);
                        for (std::size_t index = 0; index < count; ++index) {
                            const auto mode = callee->second->parameters[index].borrow_mode;
                            if (mode == BorrowMode::none) continue;
                            const auto& argument = operation.operands[index + first_argument];
                            const auto argument_provenance = provenance.find(argument);
                            if (mode == BorrowMode::mutable_) {
                                if (argument_provenance != provenance.end() && !argument_provenance->second.mutable_borrow) {
                                    diagnostics.push_back({DiagnosticSeverity::error, "cannot pass immutable borrow " + argument + " to mutable borrowed parameter of " + operation.operands[signature_index] + " in block " + current.name, {}});
                                } else if (argument_provenance == provenance.end()) {
                                    const bool already_borrowed = std::any_of(active.begin(), active.end(), [&](const std::string& id) {
                                        const auto found = borrow_info.find(id);
                                        return found != borrow_info.end() && found->second.root == argument;
                                    });
                                    if (already_borrowed) diagnostics.push_back({DiagnosticSeverity::error, "cannot mutably borrow already borrowed aggregate " + argument + " for call to " + operation.operands[signature_index] + " in block " + current.name, {}});
                                }
                            }
                        }
                    }
                }

                if (const auto created = borrow_info.find(operation.result); created != borrow_info.end()) {
                    bool any = false, mutable_active = false;
                    for (const auto& id : active) {
                        const auto found = borrow_info.find(id);
                        if (found != borrow_info.end() && found->second.root == created->second.root) {
                            if (id == created->second.parent_borrow && created->second.mutable_borrow && found->second.mutable_borrow) continue;
                            any = true;
                            mutable_active = mutable_active || found->second.mutable_borrow;
                        }
                    }
                    if (created->second.mutable_borrow && !created->second.parent_borrow.empty()) {
                        const auto parent = borrow_info.find(created->second.parent_borrow);
                        if (parent == borrow_info.end() || !parent->second.mutable_borrow)
                            diagnostics.push_back({DiagnosticSeverity::error, "mutable reborrow requires a mutable parent borrow in block " + current.name, {}});
                    }
                    if ((created->second.mutable_borrow && any) || (!created->second.mutable_borrow && mutable_active))
                        diagnostics.push_back({DiagnosticSeverity::error, "conflicting aggregate borrow of " + created->second.root + " in block " + current.name, {}});
                    if (created->second.mutable_borrow && !created->second.parent_borrow.empty()) active.erase(created->second.parent_borrow);
                    active.insert(operation.result);
                }
                if ((operation.opcode == "aggregate.move.struct" || operation.opcode == "aggregate.move.array" ||
                     operation.opcode == "aggregate.end.struct" || operation.opcode == "aggregate.end.array") && !operation.operands.empty()) {
                    const auto& root = operation.operands[0];
                    const bool borrowed = std::any_of(active.begin(), active.end(), [&](const std::string& id) {
                        const auto found = borrow_info.find(id);
                        return found != borrow_info.end() && found->second.root == root;
                    });
                    if (borrowed) diagnostics.push_back({DiagnosticSeverity::error, "cannot move or end borrowed aggregate " + root + " in block " + current.name, {}});
                }
                if ((operation.opcode == "aggregate.borrow.end.struct" || operation.opcode == "aggregate.borrow.end.array") && !operation.operands.empty()) {
                    const auto ended = borrow_info.find(operation.operands[0]);
                    if (!active.erase(operation.operands[0])) diagnostics.push_back({DiagnosticSeverity::error, "ending inactive aggregate borrow " + operation.operands[0] + " in block " + current.name, {}});
                    if (ended != borrow_info.end() && ended->second.mutable_borrow && !ended->second.parent_borrow.empty()) active.insert(ended->second.parent_borrow);
                }
            }
        }
    }
    return diagnostics;
}
}
