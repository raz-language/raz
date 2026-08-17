// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "forge/ir/module.hpp"
#include "forge/target/data_layout.hpp"

namespace forge::target {

enum class NativeAbi : std::uint8_t { system_v_x86_64, windows_x64 };
enum class AbiValueClass : std::uint8_t { none, integer, sse, memory };

struct AggregateAbiClassification {
    std::size_t size{};
    std::size_t alignment{};
    std::array<AbiValueClass, 2> classes{AbiValueClass::none, AbiValueClass::none};
    std::uint8_t register_count{};
    bool passed_indirectly{};
    bool returned_indirectly{};

    [[nodiscard]] bool register_passed() const noexcept {
        return register_count != 0 && !passed_indirectly;
    }
};

struct FunctionAbiClassification {
    AggregateAbiClassification return_value;
    std::vector<AggregateAbiClassification> parameters;
    std::size_t integer_registers{};
    std::size_t floating_registers{};
    std::size_t stack_bytes{};
    bool variadic{};
};

[[nodiscard]] const char* abi_value_class_name(AbiValueClass value) noexcept;

[[nodiscard]] std::optional<AggregateAbiClassification> classify_aggregate(
    const ir::Module& module,
    ir::AggregateRefKind kind,
    std::string_view name,
    NativeAbi abi,
    const DataLayout& layout = DataLayout::host());

[[nodiscard]] FunctionAbiClassification classify_function(
    const ir::Module& module,
    const ir::Function& function,
    NativeAbi abi,
    const DataLayout& layout = DataLayout::host());

} // namespace forge::target
