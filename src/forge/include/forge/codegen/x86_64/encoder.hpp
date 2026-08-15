// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include <functional>
#include <optional>

#include "forge/diagnostics/diagnostic.hpp"
#include "forge/machine/module.hpp"

namespace forge::codegen::x86_64 {

enum class Abi : std::uint8_t { system_v, windows };

struct CallFixup { std::size_t displacement_offset{}; std::string target; };
struct AddressFixup { std::size_t displacement_offset{}; std::string target; };
struct GlobalAddressFixup { std::size_t address_offset{}; std::string target; };
struct TlsAddressFixup { std::size_t address_offset{}; std::string target; };

struct EncodedFunction {
    std::string name;
    std::vector<std::byte> code;
    std::vector<CallFixup> calls;
    std::vector<AddressFixup> addresses;
    std::vector<GlobalAddressFixup> global_addresses;
    std::vector<TlsAddressFixup> tls_addresses;
    std::uint32_t machine_instruction_count{};
    std::uint32_t machine_instruction_count_before_optimization{};
    std::uint32_t machine_instruction_eliminated_count{};
    std::uint32_t machine_copy_propagated_count{};
    std::uint32_t machine_zero_offset_eliminated_count{};
    std::uint32_t machine_redundant_cast_eliminated_count{};
    std::uint32_t machine_address_mode_folded_count{};
    std::uint32_t machine_compare_branch_fused_count{};
    std::uint32_t machine_compare_branch_byte_avoided_count{};
    std::uint32_t machine_floating_compare_branch_fused_count{};
    std::uint32_t machine_floating_compare_branch_byte_avoided_count{};
    std::uint32_t floating_nan_guard_count{};
    std::uint32_t floating_zeroing_idiom_count{};
    std::uint32_t abi_register_argument_snapshot_count{};
    std::uint32_t abi_stack_argument_count{};
    std::uint32_t abi_shadow_space_byte_count{};
    std::uint32_t abi_alignment_padding_byte_count{};
    std::uint32_t abi_mixed_class_call_count{};
    std::uint32_t machine_jump_thread_count{};
    std::uint32_t machine_empty_block_removed_count{};
    std::uint32_t machine_unreachable_block_removed_count{};
    std::uint32_t machine_block_reordered_count{};
    std::uint32_t machine_immediate_form_selected_count{};
    std::uint32_t machine_constant_definition_eliminated_count{};
    std::uint32_t machine_immediate_comparison_selected_count{};
    std::uint32_t machine_direct_constant_return_count{};
    std::uint32_t machine_zeroing_idiom_selected_count{};
    std::uint32_t machine_constant_store_selected_count{};
    std::uint32_t machine_extension_chain_eliminated_count{};
    std::uint32_t machine_load_return_folded_count{};
    std::uint32_t machine_load_arithmetic_folded_count{};
    std::uint32_t machine_dead_instruction_eliminated_count{};
    std::uint32_t machine_dead_comparison_eliminated_count{};
    std::uint32_t machine_cross_block_copy_propagated_count{};
    std::uint32_t machine_liveness_iteration_count{};
    std::uint32_t machine_cross_block_live_value_count{};
    std::uint32_t fallthrough_jump_removed_count{};
    std::uint32_t branch_inverted_count{};
    std::uint32_t layout_byte_avoided_count{};
    std::uint32_t leaf_frame_elided_count{};
    std::uint32_t leaf_frame_byte_avoided_count{};
    std::uint32_t short_branch_count{};
    std::uint32_t forward_short_branch_count{};
    std::uint32_t short_branch_byte_avoided_count{};
    std::uint32_t short_conditional_branch_count{};
    std::uint32_t short_conditional_branch_byte_avoided_count{};
    std::uint32_t emitted_copy_count{};
    std::uint32_t eliminated_copy_count{};
    std::uint32_t two_address_reuse_count{};
    std::uint32_t unary_reuse_count{};
    std::uint32_t peak_integer_pressure{};
    std::uint32_t peak_floating_pressure{};
    std::uint32_t call_crossing_interval_count{};
    std::uint32_t caller_saved_allocation_count{};
    std::uint32_t callee_saved_allocation_count{};
    std::uint32_t weighted_spill_decision_count{};
    std::uint32_t allocation_copy_hint_count{};
    std::uint32_t segmented_interval_count{};
    std::uint32_t live_range_hole_count{};
    std::uint32_t interference_edge_count{};
    std::uint32_t hole_aware_register_reuse_count{};
    std::uint32_t live_range_split_count{};
    std::uint32_t split_transition_store_count{};
    std::uint32_t split_transition_load_count{};
    std::uint32_t split_transition_byte_count{};
    std::uint32_t encoded_byte_count{};
    std::uint32_t frame_size{};
    std::uint32_t allocated_register_count{};
    std::uint32_t spill_slot_count{};
    std::uint32_t spilled_value_count{};
    std::uint32_t reused_spill_slot_count{};
    std::uint32_t frame_size_before_slot_reuse{};
    std::uint32_t frame_bytes_saved{};
    std::uint32_t rematerialized_value_count{};
    std::uint32_t rematerialized_use_count{};
    std::uint32_t rematerialized_definition_count{};
    std::uint32_t spill_load_count{};
    std::uint32_t spill_store_count{};
    std::uint32_t redundant_spill_load_count{};
    std::uint32_t cached_spill_load_count{};
    std::uint32_t spill_store_cache_count{};
    std::uint32_t eliminated_spill_store_count{};
    std::uint32_t dead_spill_store_count{};
    std::uint32_t deferred_spill_store_count{};
    std::uint32_t folded_spill_store_load_count{};
    std::uint32_t spill_cache_generation_count{};
    std::uint32_t spill_cache_invalidation_count{};
    std::uint32_t spill_cache_hit_count{};
    std::uint32_t spill_cache_miss_count{};
    std::uint32_t spill_cache_dirty_eviction_count{};
    std::uint32_t spill_cache_clean_eviction_count{};
    std::uint32_t spill_cache_peak_resident_count{};
    std::uint32_t spill_cache_boundary_flush_count{};
    std::uint32_t spill_cache_last_use_drop_count{};
    std::uint32_t spill_cache_preserved_instruction_count{};
    std::uint32_t spill_cache_edge_preservation_count{};
    std::uint32_t avoided_spill_load_byte_count{};
    std::uint32_t avoided_spill_store_byte_count{};
    std::uint32_t pre_optimization_encoded_byte_count{};
    std::uint32_t eliminated_encoded_byte_count{};
};

struct ExternalRelocation { std::string symbol; std::size_t address_offset{}; };
enum class DataSection : std::uint8_t { read_only, writable, tls };
struct GlobalRelocation { std::size_t address_offset{}; DataSection section{}; std::size_t data_offset{}; };
struct EncodedGlobal { std::string name; DataSection section{}; std::size_t data_offset{}; };
struct ExternalGlobalRelocation { std::string symbol; std::size_t address_offset{}; };

struct EncodedModuleImage {
    std::vector<std::byte> code;
    std::vector<std::byte> read_only_data;
    std::vector<std::byte> writable_data;
    std::vector<std::byte> thread_local_data;
    std::vector<std::pair<std::string, std::size_t>> entries;
    std::vector<EncodedGlobal> globals;
    std::vector<GlobalRelocation> global_relocations;
    std::vector<ExternalGlobalRelocation> external_globals;
    std::vector<ExternalGlobalRelocation> external_tls;
    std::vector<GlobalRelocation> tls_relocations;
    std::vector<ExternalRelocation> externals;
};

struct ImageEncodeResult {
    EncodedModuleImage image;
    std::vector<EncodedFunction> functions;
    Diagnostics diagnostics;
    [[nodiscard]] bool ok() const noexcept { return diagnostics.empty(); }
};

struct EncodeResult {
    std::vector<EncodedFunction> functions;
    Diagnostics diagnostics;
    [[nodiscard]] bool ok() const noexcept { return diagnostics.empty(); }
};

[[nodiscard]] EncodeResult encode(const machine::Module& module, Abi abi);
[[nodiscard]] ImageEncodeResult encode_image(const machine::Module& module, Abi abi);
[[nodiscard]] ImageEncodeResult assemble_image(
    std::vector<EncodedFunction> functions,
    std::span<const machine::Global> globals);
using ExternalResolver = std::function<std::optional<std::uintptr_t>(std::string_view)>;
[[nodiscard]] Diagnostics resolve_externals(EncodedModuleImage& image, const ExternalResolver& resolver);
[[nodiscard]] std::string format_hex(std::span<const std::byte> code);

} // namespace forge::codegen::x86_64
