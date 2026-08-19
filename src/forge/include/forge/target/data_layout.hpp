// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <limits>
#include <optional>
#include <vector>

#include "forge/ir/module.hpp"

namespace forge::target {

enum class Endianness { little, big };

struct FieldLayout {
    std::size_t offset{};
    std::size_t size{};
    std::size_t alignment{};
};

struct StructLayout {
    std::size_t size{};
    std::size_t alignment{};
    std::vector<FieldLayout> fields;
};

struct ArrayLayout {
    std::size_t size{};
    std::size_t alignment{};
    std::size_t stride{};
};

struct DataLayout {
    std::size_t pointer_size{};
    std::size_t pointer_alignment{};
    Endianness endianness{Endianness::little};

    [[nodiscard]] static DataLayout host() noexcept;
    [[nodiscard]] bool is_valid() const noexcept;

    [[nodiscard]] std::optional<std::size_t> size_of(ir::Type type) const noexcept;
    [[nodiscard]] std::optional<std::size_t> alignment_of(ir::Type type) const noexcept;

    [[nodiscard]] std::optional<StructLayout> struct_layout(
        const ir::Module& module, const ir::StructDecl& declaration) const;
    [[nodiscard]] std::optional<ArrayLayout> array_layout(
        const ir::Module& module, const ir::ArrayDecl& declaration) const;

    [[nodiscard]] std::optional<std::size_t> aggregate_size(
        const ir::Module& module, ir::AggregateRefKind kind, const std::string& name) const;
    [[nodiscard]] std::optional<std::size_t> aggregate_alignment(
        const ir::Module& module, ir::AggregateRefKind kind, const std::string& name) const;
};

// Alignment arithmetic used by layout, ABI classification, and machine
// lowering. `checked_align_to` reports overflow rather than wrapping, which
// matters when a declared alignment is attacker- or generator-controlled.
[[nodiscard]] constexpr bool is_power_of_two(std::size_t value) noexcept {
    return value != 0 && (value & (value - 1)) == 0;
}

[[nodiscard]] constexpr bool is_aligned(std::size_t value, std::size_t alignment) noexcept {
    return is_power_of_two(alignment) && (value & (alignment - 1)) == 0;
}

[[nodiscard]] constexpr std::optional<std::size_t> checked_align_to(
    std::size_t value, std::size_t alignment) noexcept {
    if (!is_power_of_two(alignment)) return std::nullopt;
    const auto mask = alignment - 1;
    if (value > std::numeric_limits<std::size_t>::max() - mask) return std::nullopt;
    return (value + mask) & ~mask;
}

[[nodiscard]] constexpr std::size_t align_to(std::size_t value, std::size_t alignment) noexcept {
    return checked_align_to(value, alignment).value_or(value);
}

} // namespace forge::target
