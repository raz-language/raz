// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "forge/ir/module.hpp"

#include <cstddef>
#include <optional>
#include <vector>

namespace forge::target {

enum class Endianness { little, big };

[[nodiscard]] constexpr bool is_power_of_two(std::size_t value) noexcept {
    return value != 0 && (value & (value - 1U)) == 0;
}

[[nodiscard]] constexpr bool is_aligned(std::size_t value, std::size_t alignment) noexcept {
    return is_power_of_two(alignment) && (value & (alignment - 1U)) == 0;
}

[[nodiscard]] std::optional<std::size_t> checked_align_to(std::size_t value, std::size_t alignment) noexcept;

[[nodiscard]] constexpr std::size_t align_to(std::size_t value, std::size_t alignment) noexcept {
    if (!is_power_of_two(alignment)) return value;
    const auto mask = alignment - 1U;
    return (value + mask) & ~mask;
}

struct FieldLayout {
    std::size_t offset{};
    std::size_t size{};
    std::size_t alignment{1};
};

struct StructLayout {
    std::size_t size{};
    std::size_t alignment{1};
    std::vector<FieldLayout> fields;
};

struct ArrayLayout {
    std::size_t size{};
    std::size_t alignment{1};
    std::size_t stride{};
};

class DataLayout {
public:
    std::size_t pointer_size{8};
    std::size_t pointer_alignment{8};
    Endianness endianness{Endianness::little};

    [[nodiscard]] static DataLayout host() noexcept;
    [[nodiscard]] std::optional<std::size_t> size_of(ir::Type type) const noexcept;
    [[nodiscard]] std::optional<std::size_t> alignment_of(ir::Type type) const noexcept;

    [[nodiscard]] std::optional<StructLayout> struct_layout(
        const ir::Module& module, const ir::StructDecl& structure) const;
    [[nodiscard]] std::optional<ArrayLayout> array_layout(
        const ir::Module& module, const ir::ArrayDecl& array) const;
    [[nodiscard]] std::optional<std::size_t> aggregate_size(
        const ir::Module& module, ir::AggregateRefKind kind, const std::string& name) const;
    [[nodiscard]] std::optional<std::size_t> aggregate_alignment(
        const ir::Module& module, ir::AggregateRefKind kind, const std::string& name) const;
};

} // namespace forge::target
