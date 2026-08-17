// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/target/data_layout.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_set>

namespace forge::target {
namespace {

bool checked_align_up(std::size_t value, std::size_t alignment, std::size_t& result) noexcept {
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) return false;
    const auto mask = alignment - 1;
    if (value > std::numeric_limits<std::size_t>::max() - mask) return false;
    result = (value + mask) & ~mask;
    return true;
}

const ir::StructDecl* find_struct(const ir::Module& module, const std::string& name) noexcept {
    const auto it = std::find_if(module.structs().begin(), module.structs().end(),
        [&](const ir::StructDecl& item) { return item.name == name; });
    return it == module.structs().end() ? nullptr : &*it;
}

const ir::ArrayDecl* find_array(const ir::Module& module, const std::string& name) noexcept {
    const auto it = std::find_if(module.arrays().begin(), module.arrays().end(),
        [&](const ir::ArrayDecl& item) { return item.name == name; });
    return it == module.arrays().end() ? nullptr : &*it;
}

struct AggregateShape {
    std::size_t size{};
    std::size_t alignment{};
};

std::optional<AggregateShape> aggregate_shape_impl(
    const DataLayout& data_layout,
    const ir::Module& module,
    ir::AggregateRefKind kind,
    const std::string& name,
    std::unordered_set<std::string>& active);

std::optional<StructLayout> struct_layout_impl(
    const DataLayout& data_layout,
    const ir::Module& module,
    const ir::StructDecl& declaration,
    std::unordered_set<std::string>& active) {
    const std::string key = "s:" + declaration.name;
    if (!active.insert(key).second) return std::nullopt;

    StructLayout result;
    result.alignment = 1;
    result.fields.reserve(declaration.fields.size());
    std::size_t cursor = 0;

    for (const auto& field : declaration.fields) {
        std::optional<AggregateShape> shape;
        if (field.aggregate_kind == ir::AggregateRefKind::scalar) {
            const auto size = data_layout.size_of(field.type);
            const auto alignment = data_layout.alignment_of(field.type);
            if (size && alignment) shape = AggregateShape{*size, *alignment};
        } else {
            shape = aggregate_shape_impl(data_layout, module, field.aggregate_kind, field.aggregate_name, active);
        }
        if (!shape || shape->alignment == 0) {
            active.erase(key);
            return std::nullopt;
        }
        std::size_t offset = 0;
        if (!checked_align_up(cursor, shape->alignment, offset) ||
            shape->size > std::numeric_limits<std::size_t>::max() - offset) {
            active.erase(key);
            return std::nullopt;
        }
        result.fields.push_back({offset, shape->size, shape->alignment});
        cursor = offset + shape->size;
        result.alignment = std::max(result.alignment, shape->alignment);
    }

    if (!checked_align_up(cursor, result.alignment, result.size)) {
        active.erase(key);
        return std::nullopt;
    }
    active.erase(key);
    return result;
}

std::optional<ArrayLayout> array_layout_impl(
    const DataLayout& data_layout,
    const ir::Module& module,
    const ir::ArrayDecl& declaration,
    std::unordered_set<std::string>& active) {
    if (declaration.element_count == 0) return std::nullopt;
    const std::string key = "a:" + declaration.name;
    if (!active.insert(key).second) return std::nullopt;

    std::optional<AggregateShape> element;
    if (declaration.element_aggregate_kind == ir::AggregateRefKind::scalar) {
        const auto size = data_layout.size_of(declaration.element_type);
        const auto alignment = data_layout.alignment_of(declaration.element_type);
        if (size && alignment) element = AggregateShape{*size, *alignment};
    } else {
        element = aggregate_shape_impl(data_layout, module, declaration.element_aggregate_kind,
                                       declaration.element_aggregate_name, active);
    }
    if (!element || element->alignment == 0) {
        active.erase(key);
        return std::nullopt;
    }

    std::size_t stride = 0;
    if (!checked_align_up(element->size, element->alignment, stride) ||
        (stride != 0 && declaration.element_count > std::numeric_limits<std::size_t>::max() / stride)) {
        active.erase(key);
        return std::nullopt;
    }

    ArrayLayout result{stride * declaration.element_count, element->alignment, stride};
    active.erase(key);
    return result;
}

std::optional<AggregateShape> aggregate_shape_impl(
    const DataLayout& data_layout,
    const ir::Module& module,
    ir::AggregateRefKind kind,
    const std::string& name,
    std::unordered_set<std::string>& active) {
    if (kind == ir::AggregateRefKind::structure) {
        const auto* declaration = find_struct(module, name);
        if (!declaration) return std::nullopt;
        const auto layout = struct_layout_impl(data_layout, module, *declaration, active);
        if (!layout) return std::nullopt;
        return AggregateShape{layout->size, layout->alignment};
    }
    if (kind == ir::AggregateRefKind::array) {
        const auto* declaration = find_array(module, name);
        if (!declaration) return std::nullopt;
        const auto layout = array_layout_impl(data_layout, module, *declaration, active);
        if (!layout) return std::nullopt;
        return AggregateShape{layout->size, layout->alignment};
    }
    return std::nullopt;
}

} // namespace

DataLayout DataLayout::host() noexcept {
    DataLayout result;
    result.pointer_size = sizeof(void*);
    result.pointer_alignment = alignof(void*);
    result.endianness = std::endian::native == std::endian::big ? Endianness::big : Endianness::little;
    return result;
}

bool DataLayout::is_valid() const noexcept {
    return pointer_size != 0 && pointer_alignment != 0 &&
           (pointer_alignment & (pointer_alignment - 1)) == 0;
}

std::optional<std::size_t> DataLayout::size_of(ir::Type type) const noexcept {
    switch (type.kind()) {
    case ir::TypeKind::i1: return sizeof(bool);
    case ir::TypeKind::i8: return sizeof(std::int8_t);
    case ir::TypeKind::i16: return sizeof(std::int16_t);
    case ir::TypeKind::i32: return sizeof(std::int32_t);
    case ir::TypeKind::i64: return sizeof(std::int64_t);
    case ir::TypeKind::f32: return sizeof(float);
    case ir::TypeKind::f64: return sizeof(double);
    case ir::TypeKind::ptr: return pointer_size;
    case ir::TypeKind::void_: return std::nullopt;
    }
    return std::nullopt;
}

std::optional<std::size_t> DataLayout::alignment_of(ir::Type type) const noexcept {
    switch (type.kind()) {
    case ir::TypeKind::i1: return alignof(bool);
    case ir::TypeKind::i8: return alignof(std::int8_t);
    case ir::TypeKind::i16: return alignof(std::int16_t);
    case ir::TypeKind::i32: return alignof(std::int32_t);
    case ir::TypeKind::i64: return alignof(std::int64_t);
    case ir::TypeKind::f32: return alignof(float);
    case ir::TypeKind::f64: return alignof(double);
    case ir::TypeKind::ptr: return pointer_alignment;
    case ir::TypeKind::void_: return std::nullopt;
    }
    return std::nullopt;
}

std::optional<StructLayout> DataLayout::struct_layout(
    const ir::Module& module, const ir::StructDecl& declaration) const {
    std::unordered_set<std::string> active;
    return struct_layout_impl(*this, module, declaration, active);
}

std::optional<ArrayLayout> DataLayout::array_layout(
    const ir::Module& module, const ir::ArrayDecl& declaration) const {
    std::unordered_set<std::string> active;
    return array_layout_impl(*this, module, declaration, active);
}

std::optional<std::size_t> DataLayout::aggregate_size(
    const ir::Module& module, ir::AggregateRefKind kind, const std::string& name) const {
    std::unordered_set<std::string> active;
    const auto shape = aggregate_shape_impl(*this, module, kind, name, active);
    return shape ? std::optional<std::size_t>{shape->size} : std::nullopt;
}

std::optional<std::size_t> DataLayout::aggregate_alignment(
    const ir::Module& module, ir::AggregateRefKind kind, const std::string& name) const {
    std::unordered_set<std::string> active;
    const auto shape = aggregate_shape_impl(*this, module, kind, name, active);
    return shape ? std::optional<std::size_t>{shape->alignment} : std::nullopt;
}

} // namespace forge::target
