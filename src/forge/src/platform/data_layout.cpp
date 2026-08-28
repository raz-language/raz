// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/platform/data_layout.hpp"

#include <algorithm>
#include <bit>
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

struct LayoutContext {
    const DataLayout& data_layout;
    const ir::Module& module;
    std::unordered_set<std::string> active;

    std::optional<StructLayout> structure(const ir::StructDecl& declaration) {
        const std::string key = "s:" + declaration.name;
        if (!active.insert(key).second) return std::nullopt;
        struct Guard {
            std::unordered_set<std::string>& active;
            std::string key;
            ~Guard() { active.erase(key); }
        } guard{active, key};

        StructLayout result;
        result.fields.reserve(declaration.fields.size());
        std::size_t cursor = 0;
        std::size_t maximum_alignment = 1;
        for (const auto& field : declaration.fields) {
            std::optional<std::size_t> size;
            std::optional<std::size_t> alignment;
            if (field.aggregate_kind == ir::AggregateRefKind::scalar) {
                size = data_layout.size_of(field.type);
                alignment = data_layout.alignment_of(field.type);
            } else if (field.aggregate_kind == ir::AggregateRefKind::structure) {
                const auto* nested = find_struct(module, field.aggregate_name);
                if (!nested) return std::nullopt;
                const auto layout = structure(*nested);
                if (!layout) return std::nullopt;
                size = layout->size;
                alignment = layout->alignment;
            } else {
                const auto* nested = find_array(module, field.aggregate_name);
                if (!nested) return std::nullopt;
                const auto layout = array(*nested);
                if (!layout) return std::nullopt;
                size = layout->size;
                alignment = layout->alignment;
            }
            if (!size || !alignment || !is_power_of_two(*alignment)) return std::nullopt;
            const auto offset = checked_align_to(cursor, *alignment);
            if (!offset || *size > std::numeric_limits<std::size_t>::max() - *offset) return std::nullopt;
            result.fields.push_back({*offset, *size, *alignment});
            cursor = *offset + *size;
            maximum_alignment = std::max(maximum_alignment, *alignment);
        }
        const auto final_size = checked_align_to(cursor, maximum_alignment);
        if (!final_size) return std::nullopt;
        // Forge permits empty records as zero-sized compiler aggregates. They
        // consume no storage and retain byte alignment instead of borrowing the
        // host C++ ABI's implementation-defined empty-structure rule.
        result.size = *final_size;
        result.alignment = maximum_alignment;
        return result;
    }

    std::optional<ArrayLayout> array(const ir::ArrayDecl& declaration) {
        const std::string key = "a:" + declaration.name;
        if (!active.insert(key).second) return std::nullopt;
        struct Guard {
            std::unordered_set<std::string>& active;
            std::string key;
            ~Guard() { active.erase(key); }
        } guard{active, key};

        if (declaration.element_count == 0) return std::nullopt;
        std::optional<std::size_t> element_size;
        std::optional<std::size_t> element_alignment;
        if (declaration.element_aggregate_kind == ir::AggregateRefKind::scalar) {
            element_size = data_layout.size_of(declaration.element_type);
            element_alignment = data_layout.alignment_of(declaration.element_type);
        } else if (declaration.element_aggregate_kind == ir::AggregateRefKind::structure) {
            const auto* nested = find_struct(module, declaration.element_aggregate_name);
            if (!nested) return std::nullopt;
            const auto layout = structure(*nested);
            if (!layout) return std::nullopt;
            element_size = layout->size;
            element_alignment = layout->alignment;
        } else {
            const auto* nested = find_array(module, declaration.element_aggregate_name);
            if (!nested) return std::nullopt;
            const auto layout = array(*nested);
            if (!layout) return std::nullopt;
            element_size = layout->size;
            element_alignment = layout->alignment;
        }
        if (!element_size || !element_alignment || !is_power_of_two(*element_alignment)) return std::nullopt;
        const auto stride = checked_align_to(*element_size, *element_alignment);
        if (!stride || (*stride != 0 && declaration.element_count > std::numeric_limits<std::size_t>::max() / *stride))
            return std::nullopt;
        return ArrayLayout{*stride * declaration.element_count, *element_alignment, *stride};
    }
};

} // namespace

std::optional<std::size_t> checked_align_to(std::size_t value, std::size_t alignment) noexcept {
    if (!is_power_of_two(alignment)) return std::nullopt;
    const auto mask = alignment - 1U;
    if (value > std::numeric_limits<std::size_t>::max() - mask) return std::nullopt;
    return (value + mask) & ~mask;
}

DataLayout DataLayout::host() noexcept {
    DataLayout result;
    result.pointer_size = sizeof(void*);
    result.pointer_alignment = alignof(void*);
    result.endianness = std::endian::native == std::endian::big ? Endianness::big : Endianness::little;
    return result;
}

std::optional<std::size_t> DataLayout::size_of(ir::Type type) const noexcept {
    switch (type.kind()) {
    case ir::TypeKind::i1:
    case ir::TypeKind::i8: return 1U;
    case ir::TypeKind::i16: return 2U;
    case ir::TypeKind::i32:
    case ir::TypeKind::f32: return 4U;
    case ir::TypeKind::i64:
    case ir::TypeKind::f64: return 8U;
    case ir::TypeKind::ptr: return pointer_size;
    case ir::TypeKind::void_: return std::nullopt;
    }
    return std::nullopt;
}

std::optional<std::size_t> DataLayout::alignment_of(ir::Type type) const noexcept {
    switch (type.kind()) {
    case ir::TypeKind::i1:
    case ir::TypeKind::i8: return 1U;
    case ir::TypeKind::i16: return 2U;
    case ir::TypeKind::i32:
    case ir::TypeKind::f32: return 4U;
    case ir::TypeKind::i64:
    case ir::TypeKind::f64: return 8U;
    case ir::TypeKind::ptr: return pointer_alignment;
    case ir::TypeKind::void_: return std::nullopt;
    }
    return std::nullopt;
}

std::optional<StructLayout> DataLayout::struct_layout(
    const ir::Module& module, const ir::StructDecl& structure) const {
    return LayoutContext{*this, module, {}}.structure(structure);
}

std::optional<ArrayLayout> DataLayout::array_layout(
    const ir::Module& module, const ir::ArrayDecl& array) const {
    return LayoutContext{*this, module, {}}.array(array);
}

std::optional<std::size_t> DataLayout::aggregate_size(
    const ir::Module& module, ir::AggregateRefKind kind, const std::string& name) const {
    if (kind == ir::AggregateRefKind::structure) {
        const auto* declaration = find_struct(module, name);
        if (!declaration) return std::nullopt;
        const auto layout = struct_layout(module, *declaration);
        return layout ? std::optional<std::size_t>{layout->size} : std::nullopt;
    }
    if (kind == ir::AggregateRefKind::array) {
        const auto* declaration = find_array(module, name);
        if (!declaration) return std::nullopt;
        const auto layout = array_layout(module, *declaration);
        return layout ? std::optional<std::size_t>{layout->size} : std::nullopt;
    }
    return std::nullopt;
}

std::optional<std::size_t> DataLayout::aggregate_alignment(
    const ir::Module& module, ir::AggregateRefKind kind, const std::string& name) const {
    if (kind == ir::AggregateRefKind::structure) {
        const auto* declaration = find_struct(module, name);
        if (!declaration) return std::nullopt;
        const auto layout = struct_layout(module, *declaration);
        return layout ? std::optional<std::size_t>{layout->alignment} : std::nullopt;
    }
    if (kind == ir::AggregateRefKind::array) {
        const auto* declaration = find_array(module, name);
        if (!declaration) return std::nullopt;
        const auto layout = array_layout(module, *declaration);
        return layout ? std::optional<std::size_t>{layout->alignment} : std::nullopt;
    }
    return std::nullopt;
}

} // namespace forge::target
