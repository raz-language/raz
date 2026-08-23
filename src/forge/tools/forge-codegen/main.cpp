// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/diagnostics/format.hpp"
#include "forge/codegen/x86_64/encoder.hpp"
#include "forge/codegen/aarch64/encoder.hpp"
#include "forge/codegen/aarch64/register_allocation.hpp"
#include "forge/ir/parser.hpp"
#include "forge/ir/verifier.hpp"
#include "forge/machine/lower.hpp"
#include "forge/machine/module.hpp"
#include "forge/machine/register_allocation.hpp"
#include "forge/object/coff.hpp"
#include "forge/object/elf.hpp"
#include "forge/object/macho.hpp"

#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>

namespace {
void print_diagnostics(const forge::Diagnostics& diagnostics) {
    std::cerr << forge::diagnostics::render_all(diagnostics);
}
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: forge-codegen <input.fir> [--machine-ir] [--allocation] [--stats] [--image] [--emit-elf=<output.o>] [--emit-coff=<output.obj>] [--emit-macho=<output.o>] [--arch=x86_64|aarch64] [--abi=sysv|windows|aapcs64]\n";
        return 2;
    }
    bool print_machine = false;
    bool print_allocation = false;
    bool image_mode = false;
    bool print_stats = false;
    bool verify_only = false;
    std::optional<std::string> elf_output;
    std::optional<std::string> coff_output;
    std::optional<std::string> macho_output;
#if defined(_WIN32)
    auto abi = forge::codegen::x86_64::Abi::windows;
#else
    auto abi = forge::codegen::x86_64::Abi::system_v;
#endif
    auto architecture = forge::machine::TargetArchitecture::x86_64;
    for (int index = 2; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--machine-ir") print_machine = true;
        else if (argument == "--image") image_mode = true;
        else if (argument == "--allocation") print_allocation = true;
        else if (argument == "--stats") print_stats = true;
        else if (argument == "--verify-only") verify_only = true;
        else if (argument.rfind("--emit-elf=", 0) == 0) elf_output = argument.substr(11);
        else if (argument.rfind("--emit-coff=", 0) == 0) coff_output = argument.substr(12);
        else if (argument.rfind("--emit-macho=", 0) == 0) macho_output = argument.substr(13);
        else if (argument == "--abi=sysv") abi = forge::codegen::x86_64::Abi::system_v;
        else if (argument == "--abi=windows") abi = forge::codegen::x86_64::Abi::windows;
        else if (argument == "--abi=aapcs64") architecture = forge::machine::TargetArchitecture::aarch64;
        else if (argument == "--arch=x86_64") architecture = forge::machine::TargetArchitecture::x86_64;
        else if (argument == "--arch=aarch64") architecture = forge::machine::TargetArchitecture::aarch64;
        else {
            std::cerr << "unknown option: " << argument << '\n';
            return 2;
        }
    }

    std::ifstream input(argv[1], std::ios::binary);
    if (!input) {
        std::cerr << "unable to open " << argv[1] << '\n';
        return 1;
    }

    const std::string source((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    auto parsed = forge::ir::parse_module(source);
    if (!parsed.ok()) { print_diagnostics(parsed.diagnostics); return 1; }
    const auto verified = forge::ir::verify_module(*parsed.module);
    if (!verified.empty()) { print_diagnostics(verified); return 1; }
    if (verify_only) {
        std::cout << "VERIFIED  " << parsed.module->functions().size() << " functions\n";
        return 0;
    }
    auto lowered = forge::machine::lower_module(*parsed.module, {architecture});
    if (!lowered.ok()) { print_diagnostics(lowered.diagnostics); return 1; }
    if (print_machine) std::cout << forge::machine::print_module(*lowered.module) << '\n';
    if (static_cast<unsigned>(elf_output.has_value()) + static_cast<unsigned>(coff_output.has_value()) + static_cast<unsigned>(macho_output.has_value()) > 1U) { std::cerr << "choose only one object format\n"; return 2; }
    if (elf_output) {
        auto object = architecture == forge::machine::TargetArchitecture::aarch64
            ? forge::object::emit_elf64_aarch64(*lowered.module)
            : forge::object::emit_elf64_x86_64(*lowered.module, abi);
        if (!object.ok()) { print_diagnostics(object.diagnostics); return 1; }
        std::ofstream output(*elf_output, std::ios::binary);
        if (!output) { std::cerr << "unable to create " << *elf_output << '\n'; return 1; }
        output.write(reinterpret_cast<const char*>(object.bytes.data()), static_cast<std::streamsize>(object.bytes.size()));
        if (!output) { std::cerr << "unable to write " << *elf_output << '\n'; return 1; }
        std::cout << "OBJECT  ELF64 "
                  << (architecture == forge::machine::TargetArchitecture::aarch64 ? "AArch64  " : "x86-64  ")
                  << object.bytes.size() << " bytes  " << *elf_output << '\n';
    }

    if (macho_output) {
        if (architecture != forge::machine::TargetArchitecture::aarch64) {
            std::cerr << "Mach-O emission is currently implemented for arm64 only\n";
            return 2;
        }
        auto object = forge::object::emit_macho64_aarch64(*lowered.module);
        if (!object.ok()) { print_diagnostics(object.diagnostics); return 1; }
        std::ofstream output(*macho_output, std::ios::binary);
        if (!output) { std::cerr << "unable to create " << *macho_output << '\n'; return 1; }
        output.write(reinterpret_cast<const char*>(object.bytes.data()), static_cast<std::streamsize>(object.bytes.size()));
        if (!output) { std::cerr << "unable to write " << *macho_output << '\n'; return 1; }
        std::cout << "OBJECT  Mach-O arm64  " << object.bytes.size() << " bytes  " << *macho_output << '\n';
    }

    if (coff_output) {
        if (architecture == forge::machine::TargetArchitecture::aarch64) {
            std::cerr << "AArch64 COFF emission is not implemented; use --emit-elf\n";
            return 2;
        }
        auto object = forge::object::emit_coff_x86_64(*lowered.module, forge::codegen::x86_64::Abi::windows);
        if (!object.ok()) { print_diagnostics(object.diagnostics); return 1; }
        std::ofstream output(*coff_output, std::ios::binary);
        if (!output) { std::cerr << "unable to create " << *coff_output << '\n'; return 1; }
        output.write(reinterpret_cast<const char*>(object.bytes.data()), static_cast<std::streamsize>(object.bytes.size()));
        if (!output) { std::cerr << "unable to write " << *coff_output << '\n'; return 1; }
        std::cout << "OBJECT  COFF AMD64  " << object.bytes.size() << " bytes  " << *coff_output << '\n';
    }

    if (print_allocation) {
        for (const auto& function : lowered.module->functions) {
            if (architecture == forge::machine::TargetArchitecture::aarch64) {
                const auto allocation = forge::codegen::aarch64::allocate_registers(function);
                if (!allocation.ok()) { print_diagnostics(allocation.diagnostics); return 1; }
                std::cout << "ALLOC  @" << function.name
                          << "  arch=aarch64"
                          << "  physical=" << allocation.physical_value_count
                          << "  vector-physical=" << allocation.vector_register_value_count
                          << "  spills=" << allocation.spilled_value_count
                          << "  vector-spills=" << allocation.vector_spilled_value_count
                          << "  spill-bytes=" << allocation.spill_bytes
                          << "  slots=" << allocation.spill_slot_count
                          << "  reused-slots=" << allocation.reused_spill_slot_count
                          << "  frame-saved=" << allocation.frame_bytes_saved
                          << "  coalesced-copies=" << allocation.coalesced_copy_count
                          << "  hole-reuse=" << allocation.hole_aware_register_reuse_count << "\n";
            } else {
                const auto allocation = forge::machine::allocate_linear_scan(function);
                if (!allocation.ok()) { print_diagnostics(allocation.diagnostics); return 1; }
                std::cout << "ALLOC  @" << function.name
                          << "  physical=" << allocation.physical_count
                          << "  spills=" << allocation.spill_count
                          << "  slots=" << allocation.spill_slot_count
                          << "  reused-slots=" << allocation.reused_spill_slot_count
                          << "  frame-before=" << allocation.frame_size_before_slot_reuse
                          << "  frame=" << allocation.frame_size
                          << "  frame-saved=" << allocation.frame_bytes_saved
                          << "  rematerialized=" << allocation.rematerialized_value_count
                          << "  rematerialized-uses=" << allocation.rematerialized_use_count << "\n";
            }
        }
    }

    if ((elf_output || coff_output || macho_output) && !image_mode && !print_machine && !print_allocation && !print_stats) return 0;
    if (architecture == forge::machine::TargetArchitecture::aarch64) {
        if (image_mode) {
            auto encoded = forge::codegen::aarch64::encode_image(*lowered.module);
            if (!encoded.ok()) { print_diagnostics(encoded.diagnostics); return 1; }
            std::cout << "IMAGE  AArch64  " << encoded.image.code.size() << " bytes\n";
            for (const auto& entry : encoded.image.entries)
                std::cout << "ENTRY  @" << entry.first << " +" << entry.second << "\n";
            for (const auto& relocation : encoded.image.relocations)
                std::cout << "RELOC  @" << relocation.symbol << " +" << relocation.offset << "\n";
            std::cout << forge::codegen::aarch64::format_hex(encoded.image.code) << '\n';
        } else {
            auto encoded = forge::codegen::aarch64::encode(*lowered.module);
            if (!encoded.ok()) { print_diagnostics(encoded.diagnostics); return 1; }
            for (const auto& function : encoded.functions) {
                if (print_stats)
                    std::cout << "STATS  @" << function.name
                              << "  bytes=" << function.code.size()
                              << "  frame=" << function.frame_size
                              << "  register-values=" << function.register_allocated_value_count
                              << "  vector-register-values=" << function.vector_register_allocated_value_count
                              << "  spills=" << function.spilled_value_count
                              << "  vector-spills=" << function.vector_spilled_value_count
                              << "  spill-bytes=" << function.spill_bytes
                              << "  frame-saved=" << function.frame_bytes_saved
                              << "  callee-saved=" << function.callee_saved_register_count
                              << "  immediate-forms=" << function.immediate_form_count
                              << "  dead-constants-elided=" << function.elided_dead_constant_count
                              << "  neon-ops=" << function.neon_vector_operation_count
                              << "  abi-register-args=" << function.abi_register_argument_count
                              << "  abi-stack-args=" << function.abi_stack_argument_count << '\n';
                std::cout << '@' << function.name << "  " << function.code.size() << " bytes\n"
                          << forge::codegen::aarch64::format_hex(function.code) << '\n';
            }
        }
        return 0;
    }
    if (image_mode) {
        auto encoded = forge::codegen::x86_64::encode_image(*lowered.module, abi);
        if (!encoded.ok()) { print_diagnostics(encoded.diagnostics); return 1; }
        std::cout << "IMAGE  " << encoded.image.code.size() << " bytes\n";
        for (const auto& entry : encoded.image.entries)
            std::cout << "ENTRY  @" << entry.first << " +" << entry.second << "\n";
        for (const auto& relocation : encoded.image.externals)
            std::cout << "EXTERN @" << relocation.symbol << " address64 +" << relocation.address_offset << "\n";
        std::cout << forge::codegen::x86_64::format_hex(encoded.image.code) << '\n';
    } else {
        auto encoded = forge::codegen::x86_64::encode(*lowered.module, abi);
        if (!encoded.ok()) { print_diagnostics(encoded.diagnostics); return 1; }
        for (const auto& function : encoded.functions) {
            if (print_stats) {
                std::cout << "STATS  @" << function.name
                          << "  instructions=" << function.machine_instruction_count
                          << "  instructions-before-machine-opt=" << function.machine_instruction_count_before_optimization
                          << "  machine-instructions-eliminated=" << function.machine_instruction_eliminated_count
                          << "  machine-copies-propagated=" << function.machine_copy_propagated_count
                          << "  machine-zero-offsets-eliminated=" << function.machine_zero_offset_eliminated_count
                          << "  machine-redundant-casts-eliminated=" << function.machine_redundant_cast_eliminated_count
                          << "  machine-address-modes-folded=" << function.machine_address_mode_folded_count
                          << "  machine-compare-branches-fused=" << function.machine_compare_branch_fused_count
                          << "  machine-compare-branch-bytes-avoided=" << function.machine_compare_branch_byte_avoided_count
                          << "  machine-floating-compare-branches-fused=" << function.machine_floating_compare_branch_fused_count
                          << "  machine-floating-compare-branch-bytes-avoided=" << function.machine_floating_compare_branch_byte_avoided_count
                          << "  floating-nan-guards=" << function.floating_nan_guard_count
                          << "  floating-zeroing-idioms=" << function.floating_zeroing_idiom_count
                          << "  abi-register-argument-snapshots=" << function.abi_register_argument_snapshot_count
                          << "  abi-stack-arguments=" << function.abi_stack_argument_count
                          << "  abi-shadow-space-bytes=" << function.abi_shadow_space_byte_count
                          << "  abi-alignment-padding-bytes=" << function.abi_alignment_padding_byte_count
                          << "  abi-mixed-class-calls=" << function.abi_mixed_class_call_count
                          << "  machine-jump-threads=" << function.machine_jump_thread_count
                          << "  machine-empty-blocks-removed=" << function.machine_empty_block_removed_count
                          << "  machine-unreachable-blocks-removed=" << function.machine_unreachable_block_removed_count
                          << "  machine-blocks-reordered=" << function.machine_block_reordered_count
                          << "  machine-immediate-forms-selected=" << function.machine_immediate_form_selected_count
                          << "  machine-constant-definitions-eliminated=" << function.machine_constant_definition_eliminated_count
                          << "  machine-immediate-comparisons-selected=" << function.machine_immediate_comparison_selected_count
                          << "  machine-direct-constant-returns=" << function.machine_direct_constant_return_count
                          << "  machine-zeroing-idioms-selected=" << function.machine_zeroing_idiom_selected_count
                          << "  machine-constant-stores-selected=" << function.machine_constant_store_selected_count
                          << "  machine-extension-chains-eliminated=" << function.machine_extension_chain_eliminated_count
                          << "  machine-load-returns-folded=" << function.machine_load_return_folded_count
                          << "  machine-load-arithmetic-folded=" << function.machine_load_arithmetic_folded_count
                          << "  machine-dead-instructions-eliminated=" << function.machine_dead_instruction_eliminated_count
                          << "  machine-dead-comparisons-eliminated=" << function.machine_dead_comparison_eliminated_count
                          << "  machine-cross-block-copies-propagated=" << function.machine_cross_block_copy_propagated_count
                          << "  machine-liveness-iterations=" << function.machine_liveness_iteration_count
                          << "  machine-cross-block-live-values=" << function.machine_cross_block_live_value_count
                          << "  fallthrough-jumps-removed=" << function.fallthrough_jump_removed_count
                          << "  branches-inverted=" << function.branch_inverted_count
                          << "  layout-bytes-avoided=" << function.layout_byte_avoided_count
                          << "  leaf-frames-elided=" << function.leaf_frame_elided_count
                          << "  leaf-frame-bytes-avoided=" << function.leaf_frame_byte_avoided_count
                          << "  short-branches=" << function.short_branch_count
                          << "  forward-short-branches=" << function.forward_short_branch_count
                          << "  short-branch-bytes-avoided=" << function.short_branch_byte_avoided_count
                          << "  short-conditional-branches=" << function.short_conditional_branch_count
                          << "  short-conditional-branch-bytes-avoided=" << function.short_conditional_branch_byte_avoided_count
                          << "  bytes=" << function.encoded_byte_count
                          << "  frame=" << function.frame_size
                          << "  registers=" << function.allocated_register_count
                          << "  spills=" << function.spilled_value_count
                          << "  spill-slots=" << function.spill_slot_count
                          << "  spill-slots-reused=" << function.reused_spill_slot_count
                          << "  frame-before-slot-reuse=" << function.frame_size_before_slot_reuse
                          << "  frame-bytes-saved=" << function.frame_bytes_saved
                          << "  rematerialized-values=" << function.rematerialized_value_count
                          << "  rematerialized-uses=" << function.rematerialized_use_count
                          << "  rematerialized-definitions=" << function.rematerialized_definition_count
                          << "  spill-loads=" << function.spill_load_count
                          << "  spill-stores=" << function.spill_store_count
                          << "  spill-loads-eliminated=" << function.redundant_spill_load_count
                          << "  spill-loads-cached=" << function.cached_spill_load_count
                          << "  spill-stores-cached=" << function.spill_store_cache_count
                          << "  spill-stores-deferred=" << function.deferred_spill_store_count
                          << "  spill-stores-eliminated=" << function.eliminated_spill_store_count
                          << "  spill-stores-dead=" << function.dead_spill_store_count
                          << "  spill-store-load-folds=" << function.folded_spill_store_load_count
                          << "  spill-cache-generations=" << function.spill_cache_generation_count
                          << "  spill-cache-invalidations=" << function.spill_cache_invalidation_count
                          << "  spill-cache-hits=" << function.spill_cache_hit_count
                          << "  spill-cache-misses=" << function.spill_cache_miss_count
                          << "  spill-cache-dirty-evictions=" << function.spill_cache_dirty_eviction_count
                          << "  spill-cache-clean-evictions=" << function.spill_cache_clean_eviction_count
                          << "  spill-cache-peak-resident=" << function.spill_cache_peak_resident_count
                          << "  spill-cache-boundary-flushes=" << function.spill_cache_boundary_flush_count
                          << "  spill-cache-last-use-drops=" << function.spill_cache_last_use_drop_count
                          << "  spill-cache-preserved-instructions=" << function.spill_cache_preserved_instruction_count
                          << "  spill-cache-edge-preservations=" << function.spill_cache_edge_preservation_count
                          << "  spill-load-bytes-avoided=" << function.avoided_spill_load_byte_count
                          << "  spill-store-bytes-avoided=" << function.avoided_spill_store_byte_count
                          << "  bytes-before-store-elim=" << function.pre_optimization_encoded_byte_count
                          << "  bytes-eliminated=" << function.eliminated_encoded_byte_count
                          << "  copies=" << function.emitted_copy_count
                          << "  copies-eliminated=" << function.eliminated_copy_count
                          << "  two-address=" << function.two_address_reuse_count
                          << "  unary=" << function.unary_reuse_count
                          << "  peak-integer-pressure=" << function.peak_integer_pressure
                          << "  peak-floating-pressure=" << function.peak_floating_pressure
                          << "  call-crossing-intervals=" << function.call_crossing_interval_count
                          << "  caller-saved-allocations=" << function.caller_saved_allocation_count
                          << "  callee-saved-allocations=" << function.callee_saved_allocation_count
                          << "  weighted-spill-decisions=" << function.weighted_spill_decision_count
                          << "  allocation-copy-hints=" << function.allocation_copy_hint_count
                          << "  segmented-intervals=" << function.segmented_interval_count
                          << "  live-range-holes=" << function.live_range_hole_count
                          << "  interference-edges=" << function.interference_edge_count
                          << "  hole-aware-register-reuses=" << function.hole_aware_register_reuse_count << '\n'
                          << "  live-range-splits=" << function.live_range_split_count << '\n'
                          << "  split-transition-stores=" << function.split_transition_store_count << '\n'
                          << "  split-transition-loads=" << function.split_transition_load_count << '\n'
                          << "  split-transition-bytes=" << function.split_transition_byte_count << '\n';
            }
            std::cout << '@' << function.name << "  " << function.code.size() << " bytes\n"
                      << forge::codegen::x86_64::format_hex(function.code) << '\n';
        }
    }
    return 0;
}
