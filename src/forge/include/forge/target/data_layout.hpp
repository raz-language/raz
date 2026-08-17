// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
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

} // namespace forge::target
