// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include "forge/diagnostics/diagnostic.hpp"
#include "forge/ir/module.hpp"

namespace forge::ir {
inline constexpr std::uint16_t binary_format_major = 1;
inline constexpr std::uint16_t binary_format_minor = 24;

struct BinaryWriteResult {
    std::vector<std::byte> bytes;
    Diagnostics diagnostics;
    [[nodiscard]] bool ok() const noexcept { return diagnostics.empty(); }
};

struct BinaryReadResult {
    Module module;
    Diagnostics diagnostics;
    [[nodiscard]] bool ok() const noexcept { return diagnostics.empty(); }
};

[[nodiscard]] BinaryWriteResult write_binary(const Module& module);
[[nodiscard]] BinaryReadResult read_binary(std::span<const std::byte> bytes);
}
