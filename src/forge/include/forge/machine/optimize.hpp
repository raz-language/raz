// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "forge/machine/module.hpp"

namespace forge::machine {

struct OptimizationStats {
    std::uint32_t instructions_before{};
    std::uint32_t instructions_after{};
    std::uint32_t copies_propagated{};
    std::uint32_t zero_offsets_eliminated{};
    std::uint32_t redundant_casts_eliminated{};
    std::uint32_t address_modes_folded{};
    std::uint32_t compare_branches_fused{};
    std::uint32_t compare_branch_bytes_avoided{};
    std::uint32_t floating_compare_branches_fused{};
    std::uint32_t floating_compare_branch_bytes_avoided{};
    std::uint32_t jump_threads{};
    std::uint32_t empty_blocks_removed{};
    std::uint32_t unreachable_blocks_removed{};
    std::uint32_t blocks_reordered{};
    std::uint32_t immediate_forms_selected{};
    std::uint32_t constant_definitions_eliminated{};
    std::uint32_t immediate_comparisons_selected{};
    std::uint32_t direct_constant_returns{};
    std::uint32_t zeroing_idioms_selected{};
    std::uint32_t constant_stores_selected{};
    std::uint32_t extension_chains_eliminated{};
    std::uint32_t load_returns_folded{};
    std::uint32_t load_arithmetic_folded{};
    std::uint32_t dead_instructions_eliminated{};
    std::uint32_t dead_comparisons_eliminated{};
    std::uint32_t cross_block_copies_propagated{};
    std::uint32_t liveness_iterations{};
    std::uint32_t cross_block_live_values{};

    [[nodiscard]] std::uint32_t instructions_eliminated() const noexcept {
        return instructions_before - instructions_after;
    }
};

[[nodiscard]] OptimizationStats optimize_function(Function& function);
[[nodiscard]] OptimizationStats optimize_module(Module& module);

} // namespace forge::machine
