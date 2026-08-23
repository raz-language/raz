// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "forge/ir/module.hpp"
#include "forge/target/data_layout.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace forge::target {

enum class NativeAbi : std::uint8_t { system_v_x86_64, windows_x64, aapcs64 };
enum class AbiValueClass : std::uint8_t { integer, sse, memory, indirect };

[[nodiscard]] constexpr std::string_view abi_value_class_name(AbiValueClass value) noexcept {
    switch (value) {
    case AbiValueClass::integer: return "integer";
    case AbiValueClass::sse: return "sse";
    case AbiValueClass::memory: return "memory";
    case AbiValueClass::indirect: return "indirect";
    }
    return "memory";
}

struct AggregateAbiClassification {
    std::size_t size{};
    std::size_t alignment{1};
    std::uint8_t register_count{};
    bool passed_indirectly{};
    bool returned_indirectly{};
    bool homogeneous_float{};
    std::vector<AbiValueClass> classes;
    std::vector<std::uint8_t> piece_widths;

    [[nodiscard]] bool register_passed() const noexcept {
        return !passed_indirectly && register_count != 0;
    }
};

struct FunctionAbiClassification {
    bool variadic{};
    std::size_t integer_registers{};
    std::size_t floating_registers{};
    std::size_t stack_bytes{};
    std::vector<AggregateAbiClassification> parameters;
};

[[nodiscard]] std::optional<AggregateAbiClassification> classify_aggregate(
    const ir::Module& module,
    ir::AggregateRefKind kind,
    const std::string& name,
    NativeAbi abi,
    const DataLayout& data_layout = DataLayout::host());

[[nodiscard]] FunctionAbiClassification classify_function(
    const ir::Module& module,
    const ir::Function& function,
    NativeAbi abi,
    const DataLayout& data_layout = DataLayout::host());

} // namespace forge::target
