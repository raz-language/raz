// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/interpreter/interpreter.hpp"
#include "forge/platform/data_layout.hpp"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <unordered_map>

namespace forge::interpreter {
namespace {

void fail(Diagnostics& diagnostics, std::string message) {
    diagnostics.push_back({DiagnosticSeverity::error, std::move(message), {}});
}

unsigned bit_width(ir::Type type) {
    switch (type.kind()) {
    case ir::TypeKind::i1: return 1;
    case ir::TypeKind::i8: return 8;
    case ir::TypeKind::i16: return 16;
    case ir::TypeKind::i32:
    case ir::TypeKind::f32: return 32;
    case ir::TypeKind::i64:
    case ir::TypeKind::f64:
    case ir::TypeKind::ptr: return 64;
    default: return 0;
    }
}

std::uint64_t mask_for(ir::Type type) {
    const auto width = bit_width(type);
    if (width == 0 || width == 64) return std::numeric_limits<std::uint64_t>::max();
    return (std::uint64_t{1} << width) - 1;
}

std::uint64_t normalize(ir::Type type, std::uint64_t value) {
    return value & mask_for(type);
}

std::int64_t signed_bits(ir::Type type, std::uint64_t value) {
    const auto width = bit_width(type);
    value = normalize(type, value);
    if (width == 64) return static_cast<std::int64_t>(value);
    if (width == 0) return 0;
    const auto sign = std::uint64_t{1} << (width - 1);
    if ((value & sign) == 0) return static_cast<std::int64_t>(value);
    return static_cast<std::int64_t>(value | ~mask_for(type));
}

bool parse_integer(std::string_view text, std::int64_t& value) {
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size();
}

bool parse_float(std::string_view text, double& value) {
    std::string owned(text);
    char* end = nullptr;
    value = std::strtod(owned.c_str(), &end);
    return end == owned.c_str() + owned.size();
}

class Engine {
public:
    Engine(const ir::Module& module, const ExternalMap& externals, const ExternalGlobalMap& external_globals, Options options)
        : module_(module), externals_(externals), options_(options) {
        for (const auto& function : module.functions()) functions_.emplace(function.name, &function);
        for (const auto& global : module.globals()) {
            auto object = std::make_shared<MemoryObject>();
            const auto scalar_bytes = bit_width(global.type) / 8;
            const auto layout = target::DataLayout::host();
            const auto aggregate_bytes = global.is_named_aggregate()
                ? layout.aggregate_size(module_, global.aggregate_kind, global.aggregate_name)
                : std::optional<std::size_t>{};
            const auto bytes = global.is_named_aggregate() ? aggregate_bytes.value_or(0)
                : (!global.function_signature_name.empty() ? static_cast<std::size_t>(global.element_count) * sizeof(std::uintptr_t)
                   : (global.element_count != 1 ? static_cast<std::size_t>(global.element_count) : scalar_bytes));
            if (global.is_external) {
                const auto found = external_globals.find(global.name);
                if (found == external_globals.end() || !found->second.address || found->second.size < bytes) {
                    fail(diagnostics_, "unresolved external global @" + global.name);
                    continue;
                }
                object->external_data = static_cast<std::uint8_t*>(found->second.address);
                object->external_size = found->second.size;
                globals_.emplace(global.name, GlobalStorage{std::move(object), global.is_constant || found->second.read_only});
                continue;
            }
            object->bytes.resize(bytes, 0);
            if (global.element_count != 1 || global.is_named_aggregate()) {
                if (!global.zero_initialized) {
                    if (global.bytes.size() != bytes) {
                        fail(diagnostics_, "invalid byte initializer for global @" + global.name);
                        continue;
                    }
                    object->bytes = global.bytes;
                }
            } else if (!global.zero_initialized) {
                std::int64_t initializer = 0;
                if (bytes == 0 || !parse_integer(global.initializer, initializer)) {
                    fail(diagnostics_, "invalid initializer for global @" + global.name);
                    continue;
                }
                const auto bits = normalize(global.type, static_cast<std::uint64_t>(initializer));
                std::memcpy(object->bytes.data(), &bits, bytes);
            }
            globals_.emplace(global.name, GlobalStorage{std::move(object), global.is_constant});
        }
    }

    Result run(std::string_view name, std::span<const Value> arguments) {
        Result result;
        result.value = call(name, arguments, 0);
        result.diagnostics = std::move(diagnostics_);
        result.steps = steps_;
        if (!result.diagnostics.empty()) result.value.reset();
        return result;
    }

private:
    struct GlobalStorage { std::shared_ptr<MemoryObject> object; bool is_constant{}; };
    using Environment = std::unordered_map<std::string, Value>;

    std::optional<Value> call(std::string_view name, std::span<const Value> arguments, std::size_t depth) {
        if (depth >= options_.max_call_depth) {
            fail(diagnostics_, "interpreter call-depth limit exceeded");
            return std::nullopt;
        }
        const auto found = functions_.find(std::string(name));
        if (found == functions_.end()) {
            const auto external = externals_.find(std::string(name));
            if (external != externals_.end()) return external->second(arguments, diagnostics_);
            fail(diagnostics_, "unknown function @" + std::string(name));
            return std::nullopt;
        }
        const auto& function = *found->second;
        const auto layout = target::DataLayout::host();
        std::optional<Value> owned_result;
        if (function.return_owned) {
            const auto required = layout.aggregate_size(module_, function.return_aggregate_kind, function.return_aggregate_name);
            if (!required || *required == 0 || *required > (1U << 24U)) {
                fail(diagnostics_, "invalid owned aggregate return layout for @" + function.name);
                return std::nullopt;
            }
            auto object = std::make_shared<MemoryObject>();
            object->bytes.resize(*required);
            owned_result = Value::pointer({std::move(object), 0});
        }
        if (function.parameters.size() != arguments.size()) {
            fail(diagnostics_, "argument count mismatch calling @" + function.name);
            return std::nullopt;
        }
        std::vector<Value> prepared_arguments(arguments.begin(), arguments.end());
        for (std::size_t i = 0; i < function.parameters.size(); ++i) {
            const auto& parameter = function.parameters[i];
            const auto& argument = prepared_arguments[i];
            if (!parameter.is_aggregate()) continue;
            if (argument.kind() != Value::Kind::pointer) {
                fail(diagnostics_, "aggregate argument " + parameter.name + " calling @" + function.name + " must be a pointer");
                return std::nullopt;
            }
            const auto required = layout.aggregate_size(module_, parameter.aggregate_kind, parameter.aggregate_name);
            const auto alignment = layout.aggregate_alignment(module_, parameter.aggregate_kind, parameter.aggregate_name);
            if (!required || !alignment || argument.remaining_bytes() < *required || !argument.is_aligned(*alignment)) {
                fail(diagnostics_, "aggregate argument " + parameter.name + " calling @" + function.name + " has insufficient size or alignment");
                return std::nullopt;
            }
            if (parameter.owned) {
                auto object = std::make_shared<MemoryObject>();
                object->bytes.resize(*required);
                const auto& source = argument.as_pointer();
                std::memmove(object->data(), source.object->data() + source.offset, *required);
                prepared_arguments[i] = Value::pointer({std::move(object), 0});
            }
        }
        const std::span<const Value> effective_arguments(prepared_arguments);
        if (function.is_signature) { fail(diagnostics_, "cannot execute signature @" + function.name); return std::nullopt; }
        if (function.is_external) {
            const auto external = externals_.find(function.name);
            if (external == externals_.end()) {
                fail(diagnostics_, "unresolved external function @" + function.name);
                return std::nullopt;
            }
            auto returned = external->second(effective_arguments, diagnostics_);
            if (!returned || !function.returns_aggregate()) return returned;
            const auto required = layout.aggregate_size(module_, function.return_aggregate_kind, function.return_aggregate_name);
            const auto alignment = layout.aggregate_alignment(module_, function.return_aggregate_kind, function.return_aggregate_name);
            if (returned->kind() != Value::Kind::pointer || !required || !alignment ||
                returned->remaining_bytes() < *required || !returned->is_aligned(*alignment)) {
                fail(diagnostics_, "external aggregate return from @" + function.name + " has insufficient size or alignment");
                return std::nullopt;
            }
            if (function.return_owned) {
                auto& destination = owned_result->as_pointer();
                const auto& source = returned->as_pointer();
                std::memmove(destination.object->data(), source.object->data() + source.offset, *required);
                return owned_result;
            }
            return returned;
        }

        Environment values;
        for (std::size_t i = 0; i < effective_arguments.size(); ++i)
            values.emplace(function.parameters[i].name, effective_arguments[i]);
        std::size_t block_index = 0;
        std::vector<Value> incoming;

        while (true) {
            if (block_index >= function.blocks.size()) {
                fail(diagnostics_, "invalid interpreter block index in @" + function.name);
                return std::nullopt;
            }
            const auto& block = function.blocks[block_index];
            if (block.parameters.size() != incoming.size() && block_index != 0) {
                fail(diagnostics_, "block argument count mismatch entering " + block.name);
                return std::nullopt;
            }
            if (block_index != 0) {
                for (std::size_t i = 0; i < incoming.size(); ++i) {
                    const auto& parameter = block.parameters[i];
                    Value value = incoming[i];
                    if (parameter.is_aggregate()) {
                        if (value.kind() != Value::Kind::pointer) {
                            fail(diagnostics_, "aggregate block argument " + parameter.name + " must be a pointer");
                            return std::nullopt;
                        }
                        const auto required = layout.aggregate_size(module_, parameter.aggregate_kind, parameter.aggregate_name);
                        const auto alignment = layout.aggregate_alignment(module_, parameter.aggregate_kind, parameter.aggregate_name);
                        if (!required || !alignment || value.remaining_bytes() < *required || !value.is_aligned(*alignment)) {
                            fail(diagnostics_, "aggregate block argument " + parameter.name + " has insufficient size or alignment");
                            return std::nullopt;
                        }
                        if (parameter.owned) {
                            auto object = std::make_shared<MemoryObject>();
                            object->bytes.resize(*required);
                            const auto& source = value.as_pointer();
                            std::memmove(object->data(), source.object->data() + source.offset, *required);
                            value = Value::pointer({std::move(object), 0});
                        }
                    }
                    values[parameter.name] = std::move(value);
                }
            }

            bool transferred = false;
            for (const auto& operation : block.operations) {
                if (++steps_ > options_.max_steps) {
                    fail(diagnostics_, "interpreter step limit exceeded");
                    return std::nullopt;
                }
                if (operation.opcode == "return") {
                    if (operation.operands.empty()) return Value::void_value();
                    auto returned = lookup(values, operation.operands[0]);
                    if (!returned) return std::nullopt;
                    if (function.returns_aggregate()) {
                        const auto required = layout.aggregate_size(module_, function.return_aggregate_kind, function.return_aggregate_name);
                        const auto alignment = layout.aggregate_alignment(module_, function.return_aggregate_kind, function.return_aggregate_name);
                        if (returned->kind() != Value::Kind::pointer || !required || !alignment ||
                            returned->remaining_bytes() < *required || !returned->is_aligned(*alignment)) {
                            fail(diagnostics_, "aggregate return from @" + function.name + " has insufficient size or alignment");
                            return std::nullopt;
                        }
                        if (function.return_owned) {
                            auto& destination = owned_result->as_pointer();
                            const auto& source = returned->as_pointer();
                            std::memmove(destination.object->data(), source.object->data() + source.offset, *required);
                            return owned_result;
                        }
                    }
                    return returned;
                }
                if (operation.opcode == "unreachable") {
                    fail(diagnostics_, "executed unreachable operation in @" + function.name);
                    return std::nullopt;
                }
                if (operation.opcode == "jump" || operation.opcode == "branch") {
                    std::size_t successor_index = 0;
                    if (operation.opcode == "branch") {
                        const auto condition = lookup(values, operation.operands[0]);
                        if (!condition) return std::nullopt;
                        successor_index = condition->bits() != 0 ? 0 : 1;
                    }
                    const auto target = find_block(function, operation.successors[successor_index]);
                    if (!target) return std::nullopt;
                    incoming.clear();
                    for (const auto& argument : operation.successor_arguments[successor_index]) {
                        auto value = lookup(values, argument);
                        if (!value) return std::nullopt;
                        incoming.push_back(*value);
                    }
                    block_index = *target;
                    transferred = true;
                    break;
                }

                auto produced = evaluate(operation, values, depth);
                if (!diagnostics_.empty()) return std::nullopt;
                if (!operation.result.empty()) {
                    if (!produced) {
                        fail(diagnostics_, "operation '" + operation.opcode + "' did not produce a value");
                        return std::nullopt;
                    }
                    values[operation.result] = *produced;
                }
            }
            if (!transferred) {
                fail(diagnostics_, "block " + block.name + " completed without control transfer");
                return std::nullopt;
            }
        }
    }

    std::optional<Value> evaluate(const ir::Operation& op, Environment& values, std::size_t depth) {
        if (op.opcode == "const") {
            if (op.operands.size() != 1) { fail(diagnostics_, "invalid constant"); return std::nullopt; }
            if (op.type.is_float()) {
                double parsed = 0.0;
                if (!parse_float(op.operands[0], parsed)) { fail(diagnostics_, "invalid floating-point constant"); return std::nullopt; }
                return Value::floating(op.type, parsed);
            }
            std::int64_t parsed = 0;
            if (!parse_integer(op.operands[0], parsed) || !op.type.is_integer()) {
                fail(diagnostics_, "invalid integer constant"); return std::nullopt;
            }
            return Value::integer(op.type, static_cast<std::uint64_t>(parsed));
        }
        if (op.opcode == "sizeof.array" || op.opcode == "alignof.array") {
            const auto name = op.operands.at(0).substr(1);
            const auto declaration = std::find_if(module_.arrays().begin(), module_.arrays().end(), [&](const ir::ArrayDecl& item) { return item.name == name; });
            if (declaration == module_.arrays().end()) { fail(diagnostics_, "unknown array layout"); return std::nullopt; }
            const auto layout = target::DataLayout::host().array_layout(module_, *declaration);
            if (!layout) { fail(diagnostics_, "invalid array layout"); return std::nullopt; }
            return Value::integer(ir::Type(ir::TypeKind::i64), op.opcode == "sizeof.array" ? layout->size : layout->alignment);
        }
        if (op.opcode == "sizeof.struct" || op.opcode == "alignof.struct") {
            const auto name = op.operands.at(0).substr(1);
            const auto declaration = std::find_if(module_.structs().begin(), module_.structs().end(), [&](const ir::StructDecl& item) { return item.name == name; });
            if (declaration == module_.structs().end()) { fail(diagnostics_, "unknown structure @" + name); return std::nullopt; }
            const auto layout = target::DataLayout::host().struct_layout(module_, *declaration);
            if (!layout) { fail(diagnostics_, "structure layout failed for @" + name); return std::nullopt; }
            return Value::integer(ir::Type(ir::TypeKind::i64), op.opcode == "sizeof.struct" ? layout->size : layout->alignment);
        }
        if (op.opcode == "copy") return lookup(values, op.operands.at(0));
        if (op.opcode == "func.address" || op.opcode == "callback.address") return Value::function(op.operands.at(0).substr(1));
        if (op.opcode == "global.address" || op.opcode == "tls.address") {
            const auto name = op.operands.at(0).substr(1);
            const auto found = globals_.find(name);
            if (found == globals_.end()) { fail(diagnostics_, "unknown global @" + name); return std::nullopt; }
            return Value::pointer({found->second.object, 0, found->second.is_constant});
        }
        if (op.opcode == "stack.alloc.struct" || op.opcode == "stack.alloc.array") {
            const auto kind = op.opcode == "stack.alloc.struct" ? ir::AggregateRefKind::structure : ir::AggregateRefKind::array;
            const auto name = op.operands.at(0).substr(1);
            const auto size = target::DataLayout::host().aggregate_size(module_, kind, name);
            if (!size || *size == 0 || *size > (1U << 24U)) { fail(diagnostics_, "invalid typed stack allocation"); return std::nullopt; }
            auto object = std::make_shared<MemoryObject>();
            object->bytes.resize(*size);
            return Value::pointer({std::move(object), 0});
        }
        if (op.opcode == "stack.alloc") {
            std::int64_t size = 0;
            if (!parse_integer(op.operands.at(0), size) || size <= 0 || size > (1 << 24)) {
                fail(diagnostics_, "invalid interpreter stack allocation"); return std::nullopt;
            }
            auto object = std::make_shared<MemoryObject>();
            object->bytes.resize(static_cast<std::size_t>(size));
            return Value::pointer({std::move(object), 0});
        }
        if (op.opcode == "ptr.offset" || op.opcode == "field.address" || op.opcode == "struct.field.address" || op.opcode == "struct.field.name.address" || op.opcode == "array.element.address" || op.opcode == "callback.element.address") {
            auto base = lookup(values, op.operands.at(0));
            if (!base || base->kind() != Value::Kind::pointer) { fail(diagnostics_, op.opcode + " requires pointer"); return std::nullopt; }
            std::int64_t offset = 0;
            if (op.opcode == "callback.element.address") {
                std::uint64_t index = 0;
                if (op.operands.at(2).starts_with('%')) {
                    const auto dynamic_index = lookup(values, op.operands.at(2));
                    if (!dynamic_index || dynamic_index->kind() != Value::Kind::integer || dynamic_index->type() != ir::Type(ir::TypeKind::i64)) {
                        fail(diagnostics_, "invalid dynamic callback table index"); return std::nullopt;
                    }
                    index = dynamic_index->bits();
                } else {
                    std::int64_t parsed = 0;
                    if (!parse_integer(op.operands.at(2), parsed) || parsed < 0) {
                        fail(diagnostics_, "invalid callback element address"); return std::nullopt;
                    }
                    index = static_cast<std::uint64_t>(parsed);
                }
                const auto available = base->remaining_bytes() / sizeof(std::uintptr_t);
                if (index >= available || index > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) / sizeof(std::uintptr_t)) {
                    fail(diagnostics_, "callback table index out of bounds"); return std::nullopt;
                }
                offset = static_cast<std::int64_t>(index * sizeof(std::uintptr_t));
            } else if (op.opcode == "array.element.address") {
                const auto name = op.operands.at(1).substr(1);
                const auto declaration = std::find_if(module_.arrays().begin(), module_.arrays().end(), [&](const ir::ArrayDecl& item) { return item.name == name; });
                std::int64_t index = 0;
                if (declaration == module_.arrays().end()) {
                    fail(diagnostics_, "invalid array element address"); return std::nullopt;
                }
                if (op.operands.at(2).starts_with('%')) {
                    const auto dynamic_index = lookup(values, op.operands.at(2));
                    if (!dynamic_index || dynamic_index->kind() != Value::Kind::integer || dynamic_index->type() != ir::Type(ir::TypeKind::i64)) {
                        fail(diagnostics_, "invalid dynamic array index"); return std::nullopt;
                    }
                    index = static_cast<std::int64_t>(dynamic_index->bits());
                } else if (!parse_integer(op.operands.at(2), index)) {
                    fail(diagnostics_, "invalid array element address"); return std::nullopt;
                }
                if (index < 0 || static_cast<std::uint64_t>(index) >= declaration->element_count) {
                    fail(diagnostics_, "array element index out of bounds"); return std::nullopt;
                }
                const auto layout = target::DataLayout::host().array_layout(module_, *declaration);
                if (!layout || static_cast<std::uint64_t>(index) > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) / layout->stride) {
                    fail(diagnostics_, "invalid array element offset"); return std::nullopt;
                }
                offset = index * static_cast<std::int64_t>(layout->stride);
            } else if (op.opcode == "struct.field.address" || op.opcode == "struct.field.name.address") {
                const auto structure_name = op.operands.at(1).substr(1);
                const auto declaration = std::find_if(module_.structs().begin(), module_.structs().end(), [&](const ir::StructDecl& item) { return item.name == structure_name; });
                if (declaration == module_.structs().end()) { fail(diagnostics_, "invalid structure field address"); return std::nullopt; }
                std::size_t field_index = 0;
                if (op.opcode == "struct.field.address") {
                    std::int64_t parsed_index = 0;
                    if (!parse_integer(op.operands.at(2), parsed_index) || parsed_index < 0) { fail(diagnostics_, "invalid structure field index"); return std::nullopt; }
                    field_index = static_cast<std::size_t>(parsed_index);
                } else {
                    const auto field = std::find_if(declaration->fields.begin(), declaration->fields.end(), [&](const ir::StructField& item) { return item.name == op.operands.at(2); });
                    if (field == declaration->fields.end()) { fail(diagnostics_, "unknown structure field name"); return std::nullopt; }
                    field_index = static_cast<std::size_t>(field - declaration->fields.begin());
                }
                const auto layout = target::DataLayout::host().struct_layout(module_, *declaration);
                if (!layout || field_index >= layout->fields.size()) { fail(diagnostics_, "invalid structure field index"); return std::nullopt; }
                offset = static_cast<std::int64_t>(layout->fields[field_index].offset);
            } else {
                auto dynamic = lookup(values, op.operands.at(1), false);
                if (dynamic) offset = dynamic->signed_value();
                else if (!parse_integer(op.operands.at(1), offset)) { fail(diagnostics_, "invalid pointer offset"); return std::nullopt; }
            }
            if (offset < 0 || !base->as_pointer().object || base->as_pointer().offset > base->as_pointer().object->size() ||
                static_cast<std::size_t>(offset) > base->as_pointer().object->size() - base->as_pointer().offset) {
                fail(diagnostics_, "pointer offset is out of bounds"); return std::nullopt;
            }
            auto pointer = base->as_pointer(); pointer.offset += static_cast<std::size_t>(offset);
            return Value::pointer(std::move(pointer));
        }
        if (op.opcode == "load") {
            auto pointer = lookup(values, op.operands.at(0));
            if (!pointer || pointer->kind() != Value::Kind::pointer) { fail(diagnostics_, "load requires pointer"); return std::nullopt; }
            if (op.alignment != 0 && !pointer->is_aligned(op.alignment)) { fail(diagnostics_, "load address does not satisfy alignment"); return std::nullopt; }
            const auto bytes = bit_width(op.type) / 8;
            if (bytes == 0 || !pointer->as_pointer().object || pointer->as_pointer().offset > pointer->as_pointer().object->size() ||
                bytes > pointer->as_pointer().object->size() - pointer->as_pointer().offset) {
                fail(diagnostics_, "load is out of bounds"); return std::nullopt;
            }
            if (op.type == ir::Type(ir::TypeKind::ptr)) {
                const auto slot = pointer->as_pointer().object->function_slots.find(pointer->as_pointer().offset);
                if (slot != pointer->as_pointer().object->function_slots.end()) return Value::function(slot->second);
            }
            std::uint64_t bits = 0;
            std::memcpy(&bits, pointer->as_pointer().object->data() + pointer->as_pointer().offset, bytes);
            return op.type.is_float() ? Value::floating(op.type, op.type == ir::Type(ir::TypeKind::f32) ? static_cast<double>(std::bit_cast<float>(static_cast<std::uint32_t>(bits))) : std::bit_cast<double>(bits)) : Value::integer(op.type, bits);
        }
        if (op.opcode == "store") {
            auto value = lookup(values, op.operands.at(0));
            auto pointer = lookup(values, op.operands.at(1));
            if (!value || !pointer || (value->kind() != Value::Kind::integer && value->kind() != Value::Kind::floating && value->kind() != Value::Kind::function) || pointer->kind() != Value::Kind::pointer) {
                fail(diagnostics_, "store requires scalar/function value and pointer"); return std::nullopt;
            }
            if (op.alignment != 0 && !pointer->is_aligned(op.alignment)) { fail(diagnostics_, "store address does not satisfy alignment"); return std::nullopt; }
            if (pointer->as_pointer().read_only) { fail(diagnostics_, "store to constant global"); return std::nullopt; }
            const auto bytes = bit_width(op.type) / 8;
            if (bytes == 0 || !pointer->as_pointer().object || pointer->as_pointer().offset > pointer->as_pointer().object->size() ||
                bytes > pointer->as_pointer().object->size() - pointer->as_pointer().offset) {
                fail(diagnostics_, "store is out of bounds"); return std::nullopt;
            }
            if (value->kind() == Value::Kind::function) {
                if (op.type != ir::Type(ir::TypeKind::ptr)) { fail(diagnostics_, "function pointer store requires ptr type"); return std::nullopt; }
                pointer->as_pointer().object->function_slots[pointer->as_pointer().offset] = value->function_name();
                const std::uint64_t zero = 0;
                std::memcpy(pointer->as_pointer().object->data() + pointer->as_pointer().offset, &zero, bytes);
            } else {
                pointer->as_pointer().object->function_slots.erase(pointer->as_pointer().offset);
                const auto bits = value->bits();
                std::memcpy(pointer->as_pointer().object->data() + pointer->as_pointer().offset, &bits, bytes);
            }
            return std::nullopt;
        }
        if (op.opcode == "aggregate.move.struct" || op.opcode == "aggregate.move.array" ||
            op.opcode == "aggregate.borrow.struct" || op.opcode == "aggregate.borrow.array" ||
            op.opcode == "aggregate.borrow.mut.struct" || op.opcode == "aggregate.borrow.mut.array" ||
            op.opcode == "aggregate.attach.struct" || op.opcode == "aggregate.attach.array") {
            auto source = lookup(values, op.operands.at(0));
            if (!source || source->kind() != Value::Kind::pointer) {
                fail(diagnostics_, "invalid typed aggregate move"); return std::nullopt;
            }
            return *source;
        }
        if (op.opcode == "aggregate.borrow.end.struct" || op.opcode == "aggregate.borrow.end.array" ||
            op.opcode == "aggregate.end.struct" || op.opcode == "aggregate.end.array") {
            auto source = lookup(values, op.operands.at(0));
            if (!source || source->kind() != Value::Kind::pointer) {
                fail(diagnostics_, "invalid aggregate lifetime end"); return std::nullopt;
            }
            return std::nullopt;
        }
        if (op.opcode == "aggregate.copy.struct" || op.opcode == "aggregate.copy.array") {
            auto destination = lookup(values, op.operands.at(0));
            auto source = lookup(values, op.operands.at(1));
            const auto kind = op.opcode == "aggregate.copy.struct" ? ir::AggregateRefKind::structure : ir::AggregateRefKind::array;
            const auto size = target::DataLayout::host().aggregate_size(module_, kind, op.operands.at(2).substr(1));
            if (!destination || !source || !size || destination->kind() != Value::Kind::pointer || source->kind() != Value::Kind::pointer) {
                fail(diagnostics_, "invalid typed aggregate copy"); return std::nullopt;
            }
            auto& dst = destination->as_pointer(); const auto& src = source->as_pointer();
            if (dst.read_only) { fail(diagnostics_, "aggregate copy destination is read-only"); return std::nullopt; }
            if (!dst.object || !src.object || dst.offset > dst.object->size() || src.offset > src.object->size() ||
                *size > dst.object->size() - dst.offset || *size > src.object->size() - src.offset) {
                fail(diagnostics_, "aggregate copy is out of bounds"); return std::nullopt;
            }
            std::memmove(dst.object->data() + dst.offset, src.object->data() + src.offset, *size);
            return std::nullopt;
        }
        if (op.opcode == "aggregate.zero.struct" || op.opcode == "aggregate.zero.array") {
            auto destination = lookup(values, op.operands.at(0));
            const auto kind = op.opcode == "aggregate.zero.struct" ? ir::AggregateRefKind::structure : ir::AggregateRefKind::array;
            const auto size = target::DataLayout::host().aggregate_size(module_, kind, op.operands.at(1).substr(1));
            if (!destination || !size || destination->kind() != Value::Kind::pointer) { fail(diagnostics_, "invalid typed aggregate zero"); return std::nullopt; }
            auto& dst = destination->as_pointer();
            if (dst.read_only) { fail(diagnostics_, "aggregate zero destination is read-only"); return std::nullopt; }
            if (!dst.object || dst.offset > dst.object->size() || *size > dst.object->size() - dst.offset) {
                fail(diagnostics_, "aggregate zero is out of bounds"); return std::nullopt;
            }
            std::memset(dst.object->data() + dst.offset, 0, *size);
            return std::nullopt;
        }
        if (op.opcode == "memory.copy") {
            auto destination = lookup(values, op.operands.at(0));
            auto source = lookup(values, op.operands.at(1));
            auto count = lookup(values, op.operands.at(2));
            if (!destination || !source || !count || destination->kind() != Value::Kind::pointer ||
                source->kind() != Value::Kind::pointer || count->kind() != Value::Kind::integer) {
                fail(diagnostics_, "memory.copy requires destination ptr, source ptr, and integer byte count"); return std::nullopt;
            }
            auto& dst = destination->as_pointer();
            const auto& src = source->as_pointer();
            const auto bytes = static_cast<std::size_t>(count->bits());
            if (dst.read_only) { fail(diagnostics_, "memory.copy destination is read-only"); return std::nullopt; }
            if (!dst.object || !src.object || dst.offset > dst.object->size() || src.offset > src.object->size() ||
                bytes > dst.object->size() - dst.offset || bytes > src.object->size() - src.offset) {
                fail(diagnostics_, "memory.copy is out of bounds"); return std::nullopt;
            }
            std::memmove(dst.object->data() + dst.offset, src.object->data() + src.offset, bytes);
            return std::nullopt;
        }
        if (op.opcode == "memory.set") {
            auto destination = lookup(values, op.operands.at(0));
            auto value = lookup(values, op.operands.at(1));
            auto count = lookup(values, op.operands.at(2));
            if (!destination || !value || !count || destination->kind() != Value::Kind::pointer ||
                value->kind() != Value::Kind::integer || count->kind() != Value::Kind::integer) {
                fail(diagnostics_, "memory.set requires destination ptr, i8 value, and integer byte count"); return std::nullopt;
            }
            auto& dst = destination->as_pointer();
            const auto bytes = static_cast<std::size_t>(count->bits());
            if (dst.read_only) { fail(diagnostics_, "memory.set destination is read-only"); return std::nullopt; }
            if (!dst.object || dst.offset > dst.object->size() || bytes > dst.object->size() - dst.offset) {
                fail(diagnostics_, "memory.set is out of bounds"); return std::nullopt;
            }
            std::memset(dst.object->data() + dst.offset, static_cast<int>(value->bits() & 0xffU), bytes);
            return std::nullopt;
        }
        if (op.opcode == "call" || op.opcode == "call.indirect") {
            std::string target;
            std::size_t first_argument = 1;
            if (op.opcode == "call") target = op.operands.at(0).substr(1);
            else {
                auto pointer = lookup(values, op.operands.at(0));
                if (!pointer || pointer->kind() != Value::Kind::function) { fail(diagnostics_, "indirect call requires function pointer"); return std::nullopt; }
                target = pointer->function_name();
            }
            if (op.opcode == "call.indirect" && op.operands.size() > 1 && op.operands[1].starts_with("@")) first_argument = 2;
            std::vector<Value> arguments;
            for (std::size_t i = first_argument; i < op.operands.size(); ++i) {
                auto value = lookup(values, op.operands[i]); if (!value) return std::nullopt; arguments.push_back(*value);
            }
            return call(target, arguments, depth + 1);
        }
        if (op.opcode == "truncate" || op.opcode == "zero_extend" || op.opcode == "sign_extend" || op.opcode == "bitcast") {
            auto input = lookup(values, op.operands.at(0));
            if (!input || input->kind() != Value::Kind::integer) { fail(diagnostics_, op.opcode + " requires integer"); return std::nullopt; }
            std::uint64_t bits = input->bits();
            if (op.opcode == "sign_extend") bits = static_cast<std::uint64_t>(input->signed_value());
            return Value::integer(op.type, bits);
        }
        if (op.opcode == "int_to_float.signed" || op.opcode == "int_to_float.unsigned") {
            auto input = lookup(values, op.operands.at(0));
            if (!input || input->kind() != Value::Kind::integer) { fail(diagnostics_, op.opcode + " requires integer"); return std::nullopt; }
            const double number = op.opcode == "int_to_float.signed" ? static_cast<double>(input->signed_value()) : static_cast<double>(input->bits());
            return Value::floating(op.type, number);
        }
        if (op.opcode == "float_to_int.signed" || op.opcode == "float_to_int.unsigned") {
            auto input = lookup(values, op.operands.at(0));
            if (!input || input->kind() != Value::Kind::floating) { fail(diagnostics_, op.opcode + " requires floating-point input"); return std::nullopt; }
            const auto number = input->floating_value();
            const auto bits = op.opcode == "float_to_int.signed" ? static_cast<std::uint64_t>(static_cast<std::int64_t>(number)) : static_cast<std::uint64_t>(number);
            return Value::integer(op.type, bits);
        }
        if (op.opcode == "float_extend" || op.opcode == "float_truncate") {
            auto input = lookup(values, op.operands.at(0));
            if (!input || input->kind() != Value::Kind::floating) { fail(diagnostics_, op.opcode + " requires floating-point input"); return std::nullopt; }
            return Value::floating(op.type, input->floating_value());
        }

        auto lhs = lookup(values, op.operands.at(0));
        if (op.type.is_float() || (op.opcode.starts_with("cmp.") && lhs && lhs->type().is_float())) {
            if (!lhs || lhs->kind() != Value::Kind::floating) { fail(diagnostics_, op.opcode + " requires floating-point operands"); return std::nullopt; }
            if (op.opcode == "neg") return Value::floating(op.type, -lhs->floating_value());
            auto rhs = lookup(values, op.operands.at(1));
            if (!rhs || rhs->kind() != Value::Kind::floating || rhs->type() != lhs->type()) { fail(diagnostics_, op.opcode + " requires matching floating-point operands"); return std::nullopt; }
            const double a = lhs->floating_value();
            const double b = rhs->floating_value();
            if (op.opcode == "add") return Value::floating(op.type, a + b);
            if (op.opcode == "sub") return Value::floating(op.type, a - b);
            if (op.opcode == "mul") return Value::floating(op.type, a * b);
            if (op.opcode == "div") return Value::floating(op.type, a / b);
            bool comparison = false;
            if (op.opcode == "cmp.eq") comparison = a == b;
            else if (op.opcode == "cmp.ne") comparison = a != b;
            else if (op.opcode == "cmp.lt") comparison = a < b;
            else if (op.opcode == "cmp.le") comparison = a <= b;
            else if (op.opcode == "cmp.gt") comparison = a > b;
            else if (op.opcode == "cmp.ge") comparison = a >= b;
            else { fail(diagnostics_, "unsupported floating-point opcode '" + op.opcode + "'"); return std::nullopt; }
            return Value::integer(ir::Type(ir::TypeKind::i1), comparison ? 1 : 0);
        }
        if (!lhs || lhs->kind() != Value::Kind::integer) { fail(diagnostics_, op.opcode + " requires integer operands"); return std::nullopt; }
        if (op.opcode == "neg") return Value::integer(op.type, 0 - lhs->bits());
        if (op.opcode == "not") return Value::integer(op.type, ~lhs->bits());
        auto rhs = lookup(values, op.operands.at(1));
        if (!rhs || rhs->kind() != Value::Kind::integer) { fail(diagnostics_, op.opcode + " requires integer operands"); return std::nullopt; }

        const auto a = lhs->bits(); const auto b = rhs->bits();
        const auto sa = lhs->signed_value(); const auto sb = rhs->signed_value();
        if (op.opcode == "add") return Value::integer(op.type, a + b);
        if (op.opcode == "sub") return Value::integer(op.type, a - b);
        if (op.opcode == "mul") return Value::integer(op.type, a * b);
        if (op.opcode == "and") return Value::integer(op.type, a & b);
        if (op.opcode == "or") return Value::integer(op.type, a | b);
        if (op.opcode == "xor") return Value::integer(op.type, a ^ b);
        if (op.opcode == "shl") return Value::integer(op.type, a << (b & (bit_width(op.type) - 1)));
        if (op.opcode == "shr.unsigned") return Value::integer(op.type, a >> (b & (bit_width(op.type) - 1)));
        if (op.opcode == "shr.signed") return Value::integer(op.type, static_cast<std::uint64_t>(sa >> (b & (bit_width(op.type) - 1))));
        if (op.opcode == "div.signed" || op.opcode == "rem.signed") {
            if (sb == 0) { fail(diagnostics_, "signed division by zero"); return std::nullopt; }
            if (op.opcode == "div.signed") return Value::integer(op.type, static_cast<std::uint64_t>(sa / sb));
            return Value::integer(op.type, static_cast<std::uint64_t>(sa % sb));
        }
        if (op.opcode == "div.unsigned" || op.opcode == "rem.unsigned") {
            if (b == 0) { fail(diagnostics_, "unsigned division by zero"); return std::nullopt; }
            if (op.opcode == "div.unsigned") return Value::integer(op.type, a / b);
            return Value::integer(op.type, a % b);
        }

        bool comparison = false;
        if (op.opcode == "cmp.eq") comparison = a == b;
        else if (op.opcode == "cmp.ne") comparison = a != b;
        else if (op.opcode == "cmp.lt") comparison = sa < sb;
        else if (op.opcode == "cmp.le") comparison = sa <= sb;
        else if (op.opcode == "cmp.gt") comparison = sa > sb;
        else if (op.opcode == "cmp.ge") comparison = sa >= sb;
        else if (op.opcode == "cmp.ult") comparison = a < b;
        else if (op.opcode == "cmp.ule") comparison = a <= b;
        else if (op.opcode == "cmp.ugt") comparison = a > b;
        else if (op.opcode == "cmp.uge") comparison = a >= b;
        else { fail(diagnostics_, "unsupported interpreter opcode '" + op.opcode + "'"); return std::nullopt; }
        return Value::integer(ir::Type(ir::TypeKind::i1), comparison ? 1 : 0);
    }

    std::optional<Value> lookup(const Environment& values, const std::string& name, bool report = true) {
        const auto found = values.find(name);
        if (found != values.end()) return found->second;
        if (report) fail(diagnostics_, "undefined runtime value " + name);
        return std::nullopt;
    }

    std::optional<std::size_t> find_block(const ir::Function& function, const std::string& name) {
        for (std::size_t i = 0; i < function.blocks.size(); ++i) if (function.blocks[i].name == name) return i;
        fail(diagnostics_, "unknown runtime block " + name + " in @" + function.name);
        return std::nullopt;
    }

    const ir::Module& module_;
    const ExternalMap& externals_;
    Options options_;
    std::unordered_map<std::string, const ir::Function*> functions_;
    std::unordered_map<std::string, GlobalStorage> globals_;
    Diagnostics diagnostics_;
    std::size_t steps_{};
};

} // namespace

Value Value::void_value() { return {}; }
Value Value::integer(ir::Type type, std::uint64_t bits) {
    Value value; value.kind_ = Kind::integer; value.type_ = type; value.bits_ = normalize(type, bits); return value;
}

Value Value::floating(ir::Type type, double number) {
    Value value; value.kind_ = Kind::floating; value.type_ = type;
    if (type == ir::Type(ir::TypeKind::f32)) value.bits_ = std::bit_cast<std::uint32_t>(static_cast<float>(number));
    else value.bits_ = std::bit_cast<std::uint64_t>(number);
    return value;
}

Value Value::pointer(Pointer pointer) {
    Value value; value.kind_ = Kind::pointer; value.type_ = ir::Type(ir::TypeKind::ptr); value.pointer_ = std::move(pointer); return value;
}

Value Value::host_pointer(void* address, std::size_t size, bool read_only) {
    auto object = std::make_shared<MemoryObject>();
    object->external_data = static_cast<std::uint8_t*>(address);
    object->external_size = size;
    return pointer({std::move(object), 0, read_only});
}

void* Value::host_address() const noexcept {
    if (kind_ != Kind::pointer || !pointer_.object || pointer_.offset > pointer_.object->size()) return nullptr;
    return pointer_.object->data() + pointer_.offset;
}

std::size_t Value::remaining_bytes() const noexcept {
    if (kind_ != Kind::pointer || !pointer_.object || pointer_.offset > pointer_.object->size()) return 0;
    return pointer_.object->size() - pointer_.offset;
}

bool Value::is_aligned(std::size_t alignment) const noexcept {
    if (alignment == 0) return false;
    const auto address = reinterpret_cast<std::uintptr_t>(host_address());
    return address != 0 && address % alignment == 0;
}

Value Value::function(std::string name) {
    Value value; value.kind_ = Kind::function; value.type_ = ir::Type(ir::TypeKind::ptr); value.function_ = std::move(name); return value;
}

std::int64_t Value::signed_value() const noexcept { return signed_bits(type_, bits_); }
double Value::floating_value() const noexcept {
    if (type_ == ir::Type(ir::TypeKind::f32)) return static_cast<double>(std::bit_cast<float>(static_cast<std::uint32_t>(bits_)));
    if (type_ == ir::Type(ir::TypeKind::f64)) return std::bit_cast<double>(bits_);
    return 0.0;
}

Result execute(const ir::Module& module, std::string_view function, std::span<const Value> arguments,
               const ExternalMap& externals, Options options) {
    return Engine(module, externals, {}, options).run(function, arguments);
}

Result execute(const ir::Module& module, std::string_view function, std::span<const Value> arguments,
               const ExternalMap& externals, const ExternalGlobalMap& external_globals, Options options) {
    return Engine(module, externals, external_globals, options).run(function, arguments);
}

} // namespace forge::interpreter
