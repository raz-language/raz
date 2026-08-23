// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "forge/machine/module.hpp"

namespace forge::machine {

enum class X86VectorIsa : std::uint8_t {
    sse2,
    sse41,
    avx,
    avx2,
    avx512,
};

enum class SlpMemoryPattern : std::uint8_t {
    contiguous_aligned,
    contiguous_unaligned,
    broadcast,
    interleaved,
    strided,
    gather_scatter,
};

struct SlpCostModel {
    bool vector_integer_available{true};
    bool sse2{true};
    bool sse41{false};
    bool avx{false};
    bool avx2{false};
    bool avx512f{false};
    bool avx512bw{false};
    bool avx512vl{false};

    // Hardware capability and backend capability are deliberately separate, so a
    // target profile can never advertise a width the selected backend has no
    // encoder for.
    std::uint16_t vector_bits{128};
    std::uint16_t backend_vector_bits{128};
    std::uint8_t vector_register_budget{12};
    std::uint8_t mask_register_budget{0};

    // Relative cycle-like costs. Latency and reciprocal throughput are kept
    // apart so a profile can distinguish a deep dependency chain from
    // independent SLP lanes.
    double scalar_integer_latency{1.0};
    double scalar_integer_throughput{0.25};
    double vector_integer_latency{1.0};
    double vector_integer_throughput{0.50};
    double scalar_memory_cost{1.0};
    double vector_memory_cost{1.0};
    double aligned_memory_multiplier{0.90};
    double unaligned_memory_multiplier{1.00};
    double interleaved_memory_multiplier{1.35};
    double strided_memory_multiplier{2.10};
    double gather_scatter_multiplier{4.00};
    double broadcast_cost{0.60};
    double shuffle_cost{0.80};
    double vector_setup_cost{0.20};
    double register_pressure_cost{0.35};
    double minimum_speedup{1.03};

    [[nodiscard]] static SlpCostModel x86_64(X86VectorIsa isa) noexcept;
    [[nodiscard]] static SlpCostModel x86_64_sse2() noexcept { return x86_64(X86VectorIsa::sse2); }
    [[nodiscard]] static SlpCostModel x86_64_sse41() noexcept { return x86_64(X86VectorIsa::sse41); }
    [[nodiscard]] static SlpCostModel x86_64_avx() noexcept { return x86_64(X86VectorIsa::avx); }
    [[nodiscard]] static SlpCostModel x86_64_avx2() noexcept { return x86_64(X86VectorIsa::avx2); }
    [[nodiscard]] static SlpCostModel x86_64_avx512() noexcept { return x86_64(X86VectorIsa::avx512); }

    [[nodiscard]] std::uint16_t effective_vector_bits() const noexcept {
        return vector_bits < backend_vector_bits ? vector_bits : backend_vector_bits;
    }
};

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
    std::uint32_t slp_candidates_considered{};
    std::uint32_t slp_candidates_selected{};
    std::uint32_t slp_candidates_rejected_cost{};
    std::uint32_t slp_candidates_rejected_target{};
    std::uint32_t slp_width_128_selected{};
    std::uint32_t slp_width_256_selected{};
    std::uint32_t slp_width_512_selected{};
    double slp_estimated_scalar_cost{};
    double slp_estimated_vector_cost{};
    double slp_estimated_memory_cost{};
    double slp_estimated_shuffle_cost{};
    double slp_estimated_register_pressure_cost{};

    [[nodiscard]] std::uint32_t instructions_eliminated() const noexcept {
        return instructions_before - instructions_after;
    }
};

[[nodiscard]] OptimizationStats optimize_function(Function& function, const SlpCostModel& slp_cost_model = SlpCostModel::x86_64_sse2());
[[nodiscard]] OptimizationStats optimize_module(Module& module, const SlpCostModel& slp_cost_model = SlpCostModel::x86_64_sse2());

// Architecture-neutral combines used by AArch64 before target-specific
// instruction selection grows its own immediate/address/vector pseudos.
[[nodiscard]] OptimizationStats optimize_aarch64_canonical_function(Function& function);
[[nodiscard]] OptimizationStats optimize_aarch64_canonical_module(Module& module);

} // namespace forge::machine
