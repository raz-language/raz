// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/target/abi.hpp"

#include <algorithm>
#include <limits>
#include <string>
#include <unordered_set>

namespace forge::target {
namespace {

const ir::StructDecl* find_struct(const ir::Module& module, const std::string& name) {
    const auto found = std::find_if(module.structs().begin(), module.structs().end(),
        [&](const ir::StructDecl& item) { return item.name == name; });
    return found == module.structs().end() ? nullptr : &*found;
}

const ir::ArrayDecl* find_array(const ir::Module& module, const std::string& name) {
    const auto found = std::find_if(module.arrays().begin(), module.arrays().end(),
        [&](const ir::ArrayDecl& item) { return item.name == name; });
    return found == module.arrays().end() ? nullptr : &*found;
}

struct Leaf {
    std::size_t offset{};
    std::size_t size{};
    ir::Type type;
};

struct LeafCollector {
    const ir::Module& module;
    const DataLayout& data_layout;
    std::unordered_set<std::string> active;

    bool aggregate(ir::AggregateRefKind kind, const std::string& name, std::size_t base, std::vector<Leaf>& output) {
        if (kind == ir::AggregateRefKind::structure) {
            const auto* declaration = find_struct(module, name);
            if (!declaration) return false;
            return structure(*declaration, base, output);
        }
        if (kind == ir::AggregateRefKind::array) {
            const auto* declaration = find_array(module, name);
            if (!declaration) return false;
            return array(*declaration, base, output);
        }
        return false;
    }

    bool structure(const ir::StructDecl& declaration, std::size_t base, std::vector<Leaf>& output) {
        const std::string key = "s:" + declaration.name;
        if (!active.insert(key).second) return false;
        struct Guard {
            std::unordered_set<std::string>& active;
            std::string key;
            ~Guard() { active.erase(key); }
        } guard{active, key};
        const auto layout = data_layout.struct_layout(module, declaration);
        if (!layout || layout->fields.size() != declaration.fields.size()) return false;
        for (std::size_t index = 0; index < declaration.fields.size(); ++index) {
            const auto& field = declaration.fields[index];
            const auto offset = base + layout->fields[index].offset;
            if (field.aggregate_kind == ir::AggregateRefKind::scalar) {
                const auto size = data_layout.size_of(field.type);
                if (!size) return false;
                output.push_back({offset, *size, field.type});
            } else if (!aggregate(field.aggregate_kind, field.aggregate_name, offset, output)) {
                return false;
            }
        }
        return true;
    }

    bool array(const ir::ArrayDecl& declaration, std::size_t base, std::vector<Leaf>& output) {
        const std::string key = "a:" + declaration.name;
        if (!active.insert(key).second) return false;
        struct Guard {
            std::unordered_set<std::string>& active;
            std::string key;
            ~Guard() { active.erase(key); }
        } guard{active, key};
        const auto layout = data_layout.array_layout(module, declaration);
        if (!layout) return false;
        for (std::size_t index = 0; index < declaration.element_count; ++index) {
            const auto offset = base + index * layout->stride;
            if (declaration.element_aggregate_kind == ir::AggregateRefKind::scalar) {
                const auto size = data_layout.size_of(declaration.element_type);
                if (!size) return false;
                output.push_back({offset, *size, declaration.element_type});
            } else if (!aggregate(declaration.element_aggregate_kind, declaration.element_aggregate_name, offset, output)) {
                return false;
            }
        }
        return true;
    }
};

std::optional<std::pair<std::size_t, std::size_t>> aggregate_layout(
    const ir::Module& module, ir::AggregateRefKind kind, const std::string& name, const DataLayout& data_layout) {
    const auto size = data_layout.aggregate_size(module, kind, name);
    const auto alignment = data_layout.aggregate_alignment(module, kind, name);
    if (!size || !alignment) return std::nullopt;
    return std::pair{*size, *alignment};
}

bool collect_leaves(const ir::Module& module, ir::AggregateRefKind kind, const std::string& name,
                    const DataLayout& data_layout, std::vector<Leaf>& output) {
    return LeafCollector{module, data_layout, {}}.aggregate(kind, name, 0, output);
}

std::optional<std::pair<ir::Type, std::size_t>> homogeneous_float_shape(
    const std::vector<Leaf>& leaves, std::size_t aggregate_size) {
    if (leaves.empty() || leaves.size() > 4) return std::nullopt;
    const auto type = leaves.front().type;
    if (!type.is_float()) return std::nullopt;
    const auto width = type.kind() == ir::TypeKind::f32 ? std::size_t{4} : std::size_t{8};
    for (std::size_t index = 0; index < leaves.size(); ++index) {
        if (leaves[index].type != type || leaves[index].size != width || leaves[index].offset != index * width)
            return std::nullopt;
    }
    if (aggregate_size != leaves.size() * width) return std::nullopt;
    return std::pair{type, width};
}

AggregateAbiClassification indirect_classification(std::size_t size, std::size_t alignment, bool return_indirect) {
    AggregateAbiClassification result;
    result.size = size;
    result.alignment = alignment;
    result.passed_indirectly = true;
    result.returned_indirectly = return_indirect;
    result.classes = {AbiValueClass::indirect};
    return result;
}

AggregateAbiClassification classify_sysv(
    std::size_t size, std::size_t alignment, const std::vector<Leaf>& leaves) {
    if (size == 0) {
        AggregateAbiClassification result;
        result.size = 0;
        result.alignment = alignment;
        return result;
    }
    if (size > 16) {
        auto result = indirect_classification(size, alignment, true);
        result.classes[0] = AbiValueClass::memory;
        return result;
    }

    AggregateAbiClassification result;
    result.size = size;
    result.alignment = alignment;
    result.register_count = static_cast<std::uint8_t>((size + 7U) / 8U);
    result.classes.assign(result.register_count, AbiValueClass::sse);
    result.piece_widths.assign(result.register_count, 8U);
    for (std::size_t piece = 0; piece < result.register_count; ++piece) {
        const auto begin = piece * 8U;
        const auto end = std::min(size, begin + 8U);
        bool has_leaf = false;
        bool all_float = true;
        for (const auto& leaf : leaves) {
            if (leaf.offset >= end || leaf.offset + leaf.size <= begin) continue;
            has_leaf = true;
            if (!leaf.type.is_float()) all_float = false;
        }
        result.classes[piece] = has_leaf && all_float ? AbiValueClass::sse : AbiValueClass::integer;
        const auto occupied = end - begin;
        result.piece_widths[piece] = static_cast<std::uint8_t>(occupied <= 4U ? 4U : 8U);
    }
    return result;
}

AggregateAbiClassification classify_windows(std::size_t size, std::size_t alignment) {
    if (size == 1 || size == 2 || size == 4 || size == 8) {
        AggregateAbiClassification result;
        result.size = size;
        result.alignment = alignment;
        result.register_count = 1;
        result.classes = {AbiValueClass::integer};
        result.piece_widths = {static_cast<std::uint8_t>(size <= 4U ? 4U : 8U)};
        return result;
    }
    return indirect_classification(size, alignment, true);
}

AggregateAbiClassification classify_aapcs64(
    std::size_t size, std::size_t alignment, const std::vector<Leaf>& leaves) {
    if (size == 0) {
        AggregateAbiClassification result;
        result.size = 0;
        result.alignment = alignment;
        return result;
    }
    if (const auto hfa = homogeneous_float_shape(leaves, size)) {
        AggregateAbiClassification result;
        result.size = size;
        result.alignment = alignment;
        result.register_count = static_cast<std::uint8_t>(leaves.size());
        result.homogeneous_float = true;
        result.classes.assign(leaves.size(), AbiValueClass::sse);
        result.piece_widths.assign(leaves.size(), static_cast<std::uint8_t>(hfa->second));
        return result;
    }
    if (size > 16) return indirect_classification(size, alignment, true);

    AggregateAbiClassification result;
    result.size = size;
    result.alignment = alignment;
    result.register_count = static_cast<std::uint8_t>((size + 7U) / 8U);
    result.classes.assign(result.register_count, AbiValueClass::integer);
    result.piece_widths.assign(result.register_count, 8U);
    if (result.register_count != 0 && size <= 4U) result.piece_widths[0] = 4U;
    return result;
}

AggregateAbiClassification scalar_classification(ir::Type type, NativeAbi abi, const DataLayout& data_layout) {
    AggregateAbiClassification result;
    const auto size = data_layout.size_of(type).value_or(0);
    const auto alignment = data_layout.alignment_of(type).value_or(1);
    result.size = size;
    result.alignment = alignment;
    if (size == 0) return result;
    result.register_count = 1;
    result.classes = {type.is_float() ? AbiValueClass::sse : AbiValueClass::integer};
    result.piece_widths = {static_cast<std::uint8_t>(size <= 4U ? 4U : 8U)};
    (void)abi;
    return result;
}

std::size_t stack_slot_size(std::size_t size) {
    const auto rounded = checked_align_to(std::max<std::size_t>(size, 1U), 8U);
    return rounded.value_or(size);
}

} // namespace

std::optional<AggregateAbiClassification> classify_aggregate(
    const ir::Module& module,
    ir::AggregateRefKind kind,
    const std::string& name,
    NativeAbi abi,
    const DataLayout& data_layout) {
    if (kind == ir::AggregateRefKind::scalar) return std::nullopt;
    const auto layout = aggregate_layout(module, kind, name, data_layout);
    if (!layout) return std::nullopt;
    std::vector<Leaf> leaves;
    if (!collect_leaves(module, kind, name, data_layout, leaves)) return std::nullopt;
    switch (abi) {
    case NativeAbi::system_v_x86_64: return classify_sysv(layout->first, layout->second, leaves);
    case NativeAbi::windows_x64: return classify_windows(layout->first, layout->second);
    case NativeAbi::aapcs64: return classify_aapcs64(layout->first, layout->second, leaves);
    }
    return std::nullopt;
}

FunctionAbiClassification classify_function(
    const ir::Module& module,
    const ir::Function& function,
    NativeAbi abi,
    const DataLayout& data_layout) {
    FunctionAbiClassification result;
    result.variadic = function.variadic;

    std::size_t integer_used = 0;
    std::size_t floating_used = 0;
    std::size_t windows_slots = 0;
    const std::size_t integer_limit = abi == NativeAbi::system_v_x86_64 ? 6U : abi == NativeAbi::aapcs64 ? 8U : 4U;
    const std::size_t floating_limit = abi == NativeAbi::system_v_x86_64 ? 8U : abi == NativeAbi::aapcs64 ? 8U : 4U;

    for (const auto& parameter : function.parameters) {
        AggregateAbiClassification classification;
        if (parameter.is_aggregate()) {
            const auto aggregate = classify_aggregate(module, parameter.aggregate_kind, parameter.aggregate_name, abi, data_layout);
            if (!aggregate) {
                classification = indirect_classification(data_layout.pointer_size, data_layout.pointer_alignment, false);
            } else {
                classification = *aggregate;
            }
        } else {
            classification = scalar_classification(parameter.type, abi, data_layout);
        }

        if (abi == NativeAbi::windows_x64) {
            // Win64 has four ordinal argument slots shared by GP and XMM registers.
            // Indirect aggregates consume one GP slot containing the temporary address.
            if (windows_slots < 4U) {
                const bool floating = !classification.passed_indirectly && classification.register_count == 1U &&
                    !classification.classes.empty() && classification.classes[0] == AbiValueClass::sse;
                if (floating) ++floating_used;
                else ++integer_used;
            } else {
                result.stack_bytes += 8U;
            }
            ++windows_slots;
        } else if (classification.passed_indirectly) {
            if (integer_used < integer_limit) ++integer_used;
            else result.stack_bytes += 8U;
        } else {
            std::size_t needed_integer = 0;
            std::size_t needed_floating = 0;
            for (std::size_t piece = 0; piece < classification.register_count && piece < classification.classes.size(); ++piece) {
                if (classification.classes[piece] == AbiValueClass::sse) ++needed_floating;
                else ++needed_integer;
            }
            if (integer_used + needed_integer <= integer_limit && floating_used + needed_floating <= floating_limit) {
                integer_used += needed_integer;
                floating_used += needed_floating;
            } else {
                result.stack_bytes += stack_slot_size(classification.size);
            }
        }
        result.parameters.push_back(std::move(classification));
    }

    result.integer_registers = integer_used;
    result.floating_registers = floating_used;
    return result;
}

} // namespace forge::target
