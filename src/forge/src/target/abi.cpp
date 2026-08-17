// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/target/abi.hpp"

#include <algorithm>
#include <limits>
#include <string>

namespace forge::target {
namespace {

AbiValueClass merge_class(AbiValueClass left, AbiValueClass right) noexcept {
    if (left == AbiValueClass::none) return right;
    if (right == AbiValueClass::none) return left;
    if (left == AbiValueClass::memory || right == AbiValueClass::memory) return AbiValueClass::memory;
    if (left == AbiValueClass::integer || right == AbiValueClass::integer) return AbiValueClass::integer;
    return AbiValueClass::sse;
}

bool mark_range(AggregateAbiClassification& result, std::size_t offset, std::size_t size,
                AbiValueClass value_class) noexcept {
    if (size == 0 || offset > result.size || size > result.size - offset) return false;
    const auto first = offset / 8;
    const auto last = (offset + size - 1) / 8;
    if (last >= result.classes.size()) return false;
    for (std::size_t slot = first; slot <= last; ++slot)
        result.classes[slot] = merge_class(result.classes[slot], value_class);
    return true;
}

const ir::StructDecl* find_struct(const ir::Module& module, std::string_view name) noexcept {
    const auto it = std::find_if(module.structs().begin(), module.structs().end(),
        [&](const ir::StructDecl& item) { return item.name == name; });
    return it == module.structs().end() ? nullptr : &*it;
}

const ir::ArrayDecl* find_array(const ir::Module& module, std::string_view name) noexcept {
    const auto it = std::find_if(module.arrays().begin(), module.arrays().end(),
        [&](const ir::ArrayDecl& item) { return item.name == name; });
    return it == module.arrays().end() ? nullptr : &*it;
}

AbiValueClass scalar_class(ir::Type type) noexcept {
    return type.is_float() ? AbiValueClass::sse : AbiValueClass::integer;
}

bool classify_sysv_contents(const ir::Module& module, ir::AggregateRefKind kind, std::string_view name,
                            const DataLayout& layout, std::size_t base,
                            AggregateAbiClassification& result) {
    if (kind == ir::AggregateRefKind::structure) {
        const auto* declaration = find_struct(module, name);
        if (!declaration) return false;
        const auto structure = layout.struct_layout(module, *declaration);
        if (!structure || structure->fields.size() != declaration->fields.size()) return false;
        for (std::size_t index = 0; index < declaration->fields.size(); ++index) {
            const auto& field = declaration->fields[index];
            const auto& field_layout = structure->fields[index];
            if (field.aggregate_kind == ir::AggregateRefKind::scalar) {
                if (!mark_range(result, base + field_layout.offset, field_layout.size, scalar_class(field.type))) return false;
            } else if (!classify_sysv_contents(module, field.aggregate_kind, field.aggregate_name, layout,
                                               base + field_layout.offset, result)) {
                return false;
            }
        }
        return true;
    }
    if (kind == ir::AggregateRefKind::array) {
        const auto* declaration = find_array(module, name);
        if (!declaration) return false;
        const auto array = layout.array_layout(module, *declaration);
        if (!array) return false;
        for (std::size_t index = 0; index < declaration->element_count; ++index) {
            const auto offset = base + index * array->stride;
            if (declaration->element_aggregate_kind == ir::AggregateRefKind::scalar) {
                const auto size = layout.size_of(declaration->element_type);
                if (!size || !mark_range(result, offset, *size, scalar_class(declaration->element_type))) return false;
            } else if (!classify_sysv_contents(module, declaration->element_aggregate_kind,
                                               declaration->element_aggregate_name, layout, offset, result)) {
                return false;
            }
        }
        return true;
    }
    return false;
}

AggregateAbiClassification scalar_parameter(ir::Type type, const DataLayout& layout) {
    AggregateAbiClassification result;
    result.size = layout.size_of(type).value_or(0);
    result.alignment = layout.alignment_of(type).value_or(0);
    if (result.size != 0) {
        result.classes[0] = scalar_class(type);
        result.register_count = 1;
    }
    return result;
}

std::size_t stack_slot_size(const AggregateAbiClassification& value) noexcept {
    if (value.passed_indirectly) return 8;
    return std::max<std::size_t>(8, ((value.size + 7) / 8) * 8);
}

} // namespace

const char* abi_value_class_name(AbiValueClass value) noexcept {
    switch (value) {
    case AbiValueClass::none: return "none";
    case AbiValueClass::integer: return "integer";
    case AbiValueClass::sse: return "sse";
    case AbiValueClass::memory: return "memory";
    }
    return "none";
}

std::optional<AggregateAbiClassification> classify_aggregate(
    const ir::Module& module, ir::AggregateRefKind kind, std::string_view name,
    NativeAbi abi, const DataLayout& layout) {
    if (kind == ir::AggregateRefKind::scalar || name.empty()) return std::nullopt;
    const auto size = layout.aggregate_size(module, kind, std::string(name));
    const auto alignment = layout.aggregate_alignment(module, kind, std::string(name));
    if (!size || !alignment) return std::nullopt;

    AggregateAbiClassification result;
    result.size = *size;
    result.alignment = *alignment;

    if (abi == NativeAbi::windows_x64) {
        if (*size == 1 || *size == 2 || *size == 4 || *size == 8) {
            result.classes[0] = AbiValueClass::integer;
            result.register_count = 1;
        } else {
            result.classes[0] = AbiValueClass::memory;
            result.passed_indirectly = true;
            result.returned_indirectly = true;
        }
        return result;
    }

    if (*size == 0 || *size > 16) {
        result.classes[0] = AbiValueClass::memory;
        result.passed_indirectly = true;
        result.returned_indirectly = true;
        return result;
    }
    result.register_count = static_cast<std::uint8_t>((*size + 7) / 8);
    if (!classify_sysv_contents(module, kind, name, layout, 0, result)) return std::nullopt;
    for (std::size_t index = 0; index < result.register_count; ++index)
        if (result.classes[index] == AbiValueClass::none) result.classes[index] = AbiValueClass::integer;
    return result;
}

FunctionAbiClassification classify_function(
    const ir::Module& module, const ir::Function& function, NativeAbi abi, const DataLayout& layout) {
    FunctionAbiClassification result;
    result.variadic = function.variadic;
    if (function.returns_aggregate()) {
        if (const auto classified = classify_aggregate(module, function.return_aggregate_kind,
                                                       function.return_aggregate_name, abi, layout))
            result.return_value = *classified;
    } else if (function.return_type != ir::void_type()) {
        result.return_value = scalar_parameter(function.return_type, layout);
    }

    const std::size_t max_integer = abi == NativeAbi::system_v_x86_64 ? 6 : 4;
    const std::size_t max_floating = abi == NativeAbi::system_v_x86_64 ? 8 : 4;
    std::size_t windows_slots = 0;

    for (const auto& parameter : function.parameters) {
        AggregateAbiClassification value;
        if (parameter.is_aggregate()) {
            if (const auto classified = classify_aggregate(module, parameter.aggregate_kind,
                                                           parameter.aggregate_name, abi, layout))
                value = *classified;
            else {
                value.classes[0] = AbiValueClass::memory;
                value.passed_indirectly = true;
                value.returned_indirectly = true;
            }
        } else {
            value = scalar_parameter(parameter.type, layout);
        }
        result.parameters.push_back(value);

        if (abi == NativeAbi::windows_x64) {
            if (windows_slots < 4) {
                ++windows_slots;
                if (!value.passed_indirectly && value.classes[0] == AbiValueClass::sse)
                    ++result.floating_registers;
                else
                    ++result.integer_registers;
            } else {
                result.stack_bytes += stack_slot_size(value);
            }
            continue;
        }

        if (value.passed_indirectly) {
            if (result.integer_registers < max_integer) ++result.integer_registers;
            else result.stack_bytes += 8;
            continue;
        }

        std::size_t need_integer = 0;
        std::size_t need_floating = 0;
        for (std::size_t index = 0; index < value.register_count; ++index) {
            if (value.classes[index] == AbiValueClass::sse) ++need_floating;
            else ++need_integer;
        }
        if (result.integer_registers + need_integer <= max_integer &&
            result.floating_registers + need_floating <= max_floating) {
            result.integer_registers += need_integer;
            result.floating_registers += need_floating;
        } else {
            result.stack_bytes += stack_slot_size(value);
        }
    }

    return result;
}

} // namespace forge::target
