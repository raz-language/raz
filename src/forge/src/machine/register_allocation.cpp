// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/machine/register_allocation.hpp"
#include "forge/machine/liveness.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace forge::machine {
namespace {
constexpr std::uint32_t undefined_position = std::numeric_limits<std::uint32_t>::max();

void touch(std::vector<LiveInterval>& intervals, VirtualRegister reg, std::uint32_t position) {
    if (reg >= intervals.size()) return;
    auto& interval = intervals[reg];
    interval.start = std::min(interval.start, position);
    interval.end = std::max(interval.end, position);
}

std::uint32_t align_frame(std::uint32_t bytes) {
    return (bytes + 15U) & ~15U;
}

bool is_commutative_two_address(Opcode opcode) {
    switch (opcode) {
    case Opcode::add_i32: case Opcode::mul_i32: case Opcode::and_i32: case Opcode::or_i32: case Opcode::xor_i32:
    case Opcode::add_i64: case Opcode::mul_i64: case Opcode::and_i64: case Opcode::or_i64: case Opcode::xor_i64:
    case Opcode::add_f32: case Opcode::mul_f32:
    case Opcode::add_f64: case Opcode::mul_f64:
        return true;
    default:
        return false;
    }
}

bool supports_unary_reuse(Opcode opcode) {
    switch (opcode) {
    case Opcode::neg_i32: case Opcode::not_i32:
    case Opcode::neg_i64: case Opcode::not_i64:
    case Opcode::neg_f32: case Opcode::neg_f64:
        return true;
    default:
        return false;
    }
}

bool supports_two_address_reuse(Opcode opcode) {
    switch (opcode) {
    case Opcode::add_i32: case Opcode::sub_i32: case Opcode::mul_i32:
    case Opcode::and_i32: case Opcode::or_i32: case Opcode::xor_i32:
    case Opcode::add_i64: case Opcode::sub_i64: case Opcode::mul_i64:
    case Opcode::and_i64: case Opcode::or_i64: case Opcode::xor_i64:
    case Opcode::add_f32: case Opcode::sub_f32: case Opcode::mul_f32: case Opcode::div_f32:
    case Opcode::add_f64: case Opcode::sub_f64: case Opcode::mul_f64: case Opcode::div_f64:
        return true;
    default:
        return false;
    }
}

bool is_call_opcode(Opcode opcode) noexcept {
    switch (opcode) {
    case Opcode::call_i32: case Opcode::call_i64: case Opcode::call_f32: case Opcode::call_f64:
    case Opcode::call_void: case Opcode::call_aggregate: case Opcode::call_indirect_i32: case Opcode::call_indirect_i64:
    case Opcode::call_indirect_f32: case Opcode::call_indirect_f64: case Opcode::call_indirect_void:
        return true;
    default:
        return false;
    }
}

// A spill slot has to be at least as large as the value it holds. Integer and
// floating values all fit in eight bytes, but a packed value needs its full
// width or the next slot allocated below it overlaps.
std::uint32_t spill_slot_bytes(RegisterClass register_class, std::uint8_t width) noexcept {
    if (register_class != RegisterClass::vector) return 8U;
    if (width > 32U) return 64U;
    if (width > 16U) return 32U;
    return 16U;
}

Opcode split_store_opcode(RegisterClass register_class, std::uint8_t width) noexcept {
    if (register_class == RegisterClass::vector) {
        if (width > 32U) return Opcode::store_stack_v512;
        if (width > 16U) return Opcode::store_stack_v256;
        return Opcode::store_stack_v128;
    }
    if (register_class == RegisterClass::floating)
        return width == 8U ? Opcode::store_stack_f64 : Opcode::store_stack_f32;
    if (width <= 1U) return Opcode::store_stack_i8;
    if (width <= 2U) return Opcode::store_stack_i16;
    if (width <= 4U) return Opcode::store_stack_i32;
    return Opcode::store_stack_i64;
}

Opcode split_load_opcode(RegisterClass register_class, std::uint8_t width) noexcept {
    if (register_class == RegisterClass::vector) {
        if (width > 32U) return Opcode::load_stack_v512;
        if (width > 16U) return Opcode::load_stack_v256;
        return Opcode::load_stack_v128;
    }
    if (register_class == RegisterClass::floating)
        return width == 8U ? Opcode::load_stack_f64 : Opcode::load_stack_f32;
    if (width <= 1U) return Opcode::load_stack_i8;
    if (width <= 2U) return Opcode::load_stack_i16;
    if (width <= 4U) return Opcode::load_stack_i32;
    return Opcode::load_stack_i64;
}

void rewrite_register(Instruction& instruction, VirtualRegister from, VirtualRegister to) {
    for (auto& input : instruction.inputs)
        if (input == from) input = to;
    for (auto& successor : instruction.successors)
        for (auto& argument : successor.arguments)
            if (argument == from) argument = to;
}

bool produces_result(Opcode opcode) {
    switch (opcode) {
    case Opcode::store_stack_i8: case Opcode::store_stack_i16: case Opcode::store_stack_i32:
    case Opcode::store_stack_i64: case Opcode::store_stack_f32: case Opcode::store_stack_f64:
    case Opcode::store_ptr_i8: case Opcode::store_ptr_i16: case Opcode::store_ptr_i32:
    case Opcode::store_ptr_i64: case Opcode::store_ptr_f32: case Opcode::store_ptr_f64:
    case Opcode::call_void: case Opcode::call_aggregate: case Opcode::call_indirect_void:
    case Opcode::jump: case Opcode::branch_i1:
    case Opcode::return_i32: case Opcode::return_i64: case Opcode::return_f32:
    case Opcode::return_f64: case Opcode::return_void: case Opcode::return_aggregate:
        return false;
    default:
        return true;
    }
}
} // namespace

LiveRangeSplitStats split_live_ranges_around_calls(Function& function) {
    LiveRangeSplitStats stats;
    if (function.blocks.empty() || function.register_count == 0U) return stats;

    // This is an optional code-quality transform, not a correctness requirement.
    // Each split class now consumes one whole-function liveness snapshot rather
    // than iterating to a fixed point, but the dense block_count x register_count
    // matrices can still be expensive for generated compiler functions. The
    // allocator already handles call-crossing values via callee-saved registers
    // and spills, so keep a problem-size cutoff for very large functions. A
    // register-only cutoff misses compiler-shaped functions with 1-2k values
    // spread across hundreds of blocks, where even a few optional full-function
    // analyses can dominate object emission.
    // The baseline allocator is already correct without splitting, so bound the
    // transform by the actual liveness problem size instead.
    constexpr std::size_t max_split_liveness_cells = 100000U;
    const auto liveness_cells = static_cast<std::uint64_t>(function.register_count) *
                                static_cast<std::uint64_t>(function.blocks.size());
    if (function.register_count > 2048U || liveness_cells > max_split_liveness_cells) return stats;

    // Split only where the current ABI model has no cheap register solution:
    // floating values live across a call and integer pressure above the two
    // available callee-saved registers. Same-block splitting is intentionally
    // processed backwards from one immutable liveness snapshot. An earlier
    // split then rewrites the transition store of any later split, so values
    // crossing several calls form the correct reload chain without rebuilding
    // whole-function liveness after each local insertion.
    const auto same_block_register_count = function.register_count;
    const auto same_block_liveness = analyze_liveness(function);
    const auto same_block_intervals = compute_live_intervals(function, same_block_liveness);
    std::vector<std::vector<std::size_t>> same_block_last_use(function.blocks.size(),
        std::vector<std::size_t>(same_block_register_count, 0U));
    std::vector<std::vector<std::uint8_t>> same_block_has_use(function.blocks.size(),
        std::vector<std::uint8_t>(same_block_register_count, 0U));
    for (std::size_t block_index = 0; block_index < function.blocks.size(); ++block_index) {
        const auto& block = function.blocks[block_index];
        for (std::size_t instruction_index = 0; instruction_index < block.instructions.size(); ++instruction_index) {
            const auto& instruction = block.instructions[instruction_index];
            const auto record_use = [&](VirtualRegister reg) {
                if (reg >= same_block_register_count) return;
                same_block_has_use[block_index][reg] = 1U;
                same_block_last_use[block_index][reg] = instruction_index;
            };
            for (const auto reg : instruction.inputs) record_use(reg);
            for (const auto& edge : instruction.successors)
                for (const auto reg : edge.arguments) record_use(reg);
        }
    }

    for (std::size_t block_index = 0; block_index < function.blocks.size(); ++block_index) {
        auto& block = function.blocks[block_index];
        std::vector<std::size_t> call_sites;
        call_sites.reserve(block.instructions.size() / 8U + 1U);
        for (std::size_t instruction_index = 0; instruction_index < block.instructions.size(); ++instruction_index)
            if (is_call_opcode(block.instructions[instruction_index].opcode)) call_sites.push_back(instruction_index);

        for (auto call_it = call_sites.rbegin(); call_it != call_sites.rend(); ++call_it) {
            const auto instruction_index = *call_it;
            const auto call_result = block.instructions[instruction_index].result;
            std::vector<VirtualRegister> floating_candidates;
            std::vector<VirtualRegister> integer_candidates;
            for (VirtualRegister reg = 0; reg < same_block_register_count; ++reg) {
                if (!same_block_liveness.live_after[block_index][instruction_index][reg] ||
                    same_block_liveness.live_out[block_index][reg] || reg == call_result) continue;
                if (!same_block_has_use[block_index][reg] ||
                    same_block_last_use[block_index][reg] <= instruction_index) continue;
                const bool floating = reg < function.register_classes.size() &&
                                      uses_vector_register_file(function.register_classes[reg]);
                (floating ? floating_candidates : integer_candidates).push_back(reg);
            }

            std::stable_sort(integer_candidates.begin(), integer_candidates.end(), [&](VirtualRegister left, VirtualRegister right) {
                if (same_block_intervals[left].spill_weight != same_block_intervals[right].spill_weight)
                    return same_block_intervals[left].spill_weight < same_block_intervals[right].spill_weight;
                return left < right;
            });
            std::vector<VirtualRegister> selected = floating_candidates;
            if (integer_candidates.size() > 2U)
                selected.insert(selected.end(), integer_candidates.begin(), integer_candidates.end() - 2);
            if (selected.empty()) continue;

            std::vector<Instruction> stores;
            std::vector<Instruction> loads;
            stores.reserve(selected.size());
            loads.reserve(selected.size());
            std::vector<std::pair<VirtualRegister, VirtualRegister>> replacements;
            replacements.reserve(selected.size());
            for (const auto reg : selected) {
                const auto width = reg < function.register_widths.size() ? function.register_widths[reg] : std::uint8_t{64};
                const auto register_class = reg < function.register_classes.size()
                    ? function.register_classes[reg] : RegisterClass::integer;
                const Instruction* rematerializable = nullptr;
                for (std::size_t earlier = instruction_index; earlier-- > 0U;) {
                    const auto& candidate = block.instructions[earlier];
                    if (candidate.result != reg) continue;
                    if (candidate.opcode == Opcode::load_immediate_f32 ||
                        candidate.opcode == Opcode::load_immediate_f64)
                        rematerializable = &candidate;
                    break;
                }
                const auto replacement = function.register_count++;
                function.register_widths.push_back(width);
                function.register_classes.push_back(register_class);
                if (rematerializable != nullptr) {
                    auto reload = *rematerializable;
                    reload.result = replacement;
                    loads.push_back(std::move(reload));
                    ++stats.transition_loads;
                    stats.transition_bytes += 8U;
                } else {
                    function.local_stack_size += spill_slot_bytes(register_class, width);
                    const auto offset = -static_cast<std::int64_t>(function.local_stack_size);
                    stores.push_back({split_store_opcode(register_class, width), 0U, {reg}, offset, 0U, {}, {}});
                    loads.push_back({split_load_opcode(register_class, width), replacement, {}, offset, 0U, {}, {}});
                    ++stats.transition_stores;
                    ++stats.transition_loads;
                    stats.transition_bytes += 16U;
                }
                replacements.emplace_back(reg, replacement);
                ++stats.split_values;
            }

            block.instructions.insert(block.instructions.begin() + static_cast<std::ptrdiff_t>(instruction_index),
                                      stores.begin(), stores.end());
            const auto call_index = instruction_index + stores.size();
            block.instructions.insert(block.instructions.begin() + static_cast<std::ptrdiff_t>(call_index + 1U),
                                      loads.begin(), loads.end());
            const auto rewrite_start = call_index + 1U + loads.size();
            for (std::size_t later = rewrite_start; later < block.instructions.size(); ++later)
                for (const auto& [from, to] : replacements)
                    rewrite_register(block.instructions[later], from, to);
        }
    }
    // Extend splitting across a simple CFG continuation. This phase also uses
    // one analysis snapshot. Calls are visited backwards and each value is
    // split at most once for a given continuation, which picks the latest call
    // that actually needs the transition and avoids fixed-point reanalysis.
    {
        const auto cross_register_count = function.register_count;
        const auto liveness = analyze_liveness(function);
        const auto intervals = compute_live_intervals(function, liveness);
        std::unordered_map<std::string, std::size_t> block_indices;
        std::vector<std::uint32_t> predecessor_counts(function.blocks.size(), 0U);
        for (std::size_t index = 0; index < function.blocks.size(); ++index)
            block_indices.emplace(function.blocks[index].name, index);
        for (const auto& block : function.blocks) {
            if (block.instructions.empty()) continue;
            for (const auto& successor : block.instructions.back().successors) {
                const auto target = block_indices.find(successor.block);
                if (target != block_indices.end()) ++predecessor_counts[target->second];
            }
        }
        std::vector<std::vector<std::size_t>> last_use(function.blocks.size(),
            std::vector<std::size_t>(cross_register_count, 0U));
        std::vector<std::vector<std::uint8_t>> has_use(function.blocks.size(),
            std::vector<std::uint8_t>(cross_register_count, 0U));
        for (std::size_t use_block_index = 0; use_block_index < function.blocks.size(); ++use_block_index) {
            const auto& use_block = function.blocks[use_block_index];
            for (std::size_t use_instruction_index = 0; use_instruction_index < use_block.instructions.size(); ++use_instruction_index) {
                const auto& use_instruction = use_block.instructions[use_instruction_index];
                const auto record_use = [&](VirtualRegister reg) {
                    if (reg >= cross_register_count) return;
                    has_use[use_block_index][reg] = 1U;
                    last_use[use_block_index][reg] = use_instruction_index;
                };
                for (const auto reg : use_instruction.inputs) record_use(reg);
                for (const auto& edge : use_instruction.successors)
                    for (const auto reg : edge.arguments) record_use(reg);
            }
        }

        for (std::size_t block_index = 0; block_index < function.blocks.size(); ++block_index) {
            auto& block = function.blocks[block_index];
            if (block.instructions.empty() || block.instructions.back().successors.size() != 1U) continue;
            const auto successor_it = block_indices.find(block.instructions.back().successors.front().block);
            if (successor_it == block_indices.end() || predecessor_counts[successor_it->second] != 1U) continue;
            const auto successor_index = successor_it->second;

            std::vector<std::uint8_t> transitioned(cross_register_count, 0U);
            std::vector<std::size_t> call_sites;
            for (std::size_t instruction_index = 0; instruction_index < block.instructions.size(); ++instruction_index)
                if (is_call_opcode(block.instructions[instruction_index].opcode)) call_sites.push_back(instruction_index);

            for (auto call_it = call_sites.rbegin(); call_it != call_sites.rend(); ++call_it) {
                const auto instruction_index = *call_it;
                const auto call_result = block.instructions[instruction_index].result;
                std::vector<VirtualRegister> floating_candidates;
                std::vector<VirtualRegister> integer_candidates;
                for (VirtualRegister reg = 0; reg < cross_register_count; ++reg) {
                    if (transitioned[reg] || !liveness.live_after[block_index][instruction_index][reg] ||
                        !liveness.live_out[block_index][reg] || reg == call_result) continue;
                    if (has_use[block_index][reg] && last_use[block_index][reg] > instruction_index) continue;
                    if (!has_use[successor_index][reg]) continue;
                    const bool floating = reg < function.register_classes.size() &&
                                          uses_vector_register_file(function.register_classes[reg]);
                    (floating ? floating_candidates : integer_candidates).push_back(reg);
                }

                std::stable_sort(integer_candidates.begin(), integer_candidates.end(), [&](VirtualRegister left, VirtualRegister right) {
                    if (intervals[left].spill_weight != intervals[right].spill_weight)
                        return intervals[left].spill_weight < intervals[right].spill_weight;
                    return left < right;
                });
                std::vector<VirtualRegister> selected = floating_candidates;
                if (integer_candidates.size() > 2U)
                    selected.insert(selected.end(), integer_candidates.begin(), integer_candidates.end() - 2);
                if (selected.empty()) continue;

                std::vector<Instruction> stores;
                std::vector<Instruction> loads;
                std::vector<std::pair<VirtualRegister, VirtualRegister>> replacements;
                for (const auto reg : selected) {
                    transitioned[reg] = 1U;
                    const auto width = reg < function.register_widths.size() ? function.register_widths[reg] : std::uint8_t{64};
                    const auto register_class = reg < function.register_classes.size()
                        ? function.register_classes[reg] : RegisterClass::integer;
                    function.local_stack_size += spill_slot_bytes(register_class, width);
                    const auto offset = -static_cast<std::int64_t>(function.local_stack_size);
                    stores.push_back({split_store_opcode(register_class, width), 0U, {reg}, offset, 0U, {}, {}});
                    const auto replacement = function.register_count++;
                    function.register_widths.push_back(width);
                    function.register_classes.push_back(register_class);
                    loads.push_back({split_load_opcode(register_class, width), replacement, {}, offset, 0U, {}, {}});
                    replacements.emplace_back(reg, replacement);
                    ++stats.split_values;
                    ++stats.cross_block_split_values;
                    ++stats.transition_stores;
                    ++stats.transition_loads;
                    stats.transition_bytes += 16U;
                }

                block.instructions.insert(block.instructions.begin() + static_cast<std::ptrdiff_t>(instruction_index),
                                          stores.begin(), stores.end());
                auto& successor_block = function.blocks[successor_index];
                successor_block.instructions.insert(successor_block.instructions.begin(), loads.begin(), loads.end());
                for (std::size_t later = loads.size(); later < successor_block.instructions.size(); ++later)
                    for (const auto& [from, to] : replacements)
                        rewrite_register(successor_block.instructions[later], from, to);
            }
        }
    }

    // Extend splitting through multi-predecessor continuations by introducing
    // an explicit edge block and a block parameter in the merge block. Critical
    // edges are structural, so take one fresh snapshot after the simpler split
    // phases and batch at most one split set per merge continuation. The normal
    // allocator remains the fallback for other incoming call paths; correctness
    // never depends on repeatedly rebuilding liveness to discover more splits.
    {
        const auto critical_register_count = function.register_count;
        const auto critical_block_count = function.blocks.size();
        const auto liveness = analyze_liveness(function);
        const auto intervals = compute_live_intervals(function, liveness);
        std::unordered_map<std::string, std::size_t> block_indices;
        std::vector<std::uint32_t> predecessor_counts(critical_block_count, 0U);
        for (std::size_t index = 0; index < critical_block_count; ++index)
            block_indices.emplace(function.blocks[index].name, index);
        for (std::size_t index = 0; index < critical_block_count; ++index) {
            const auto& candidate_block = function.blocks[index];
            if (candidate_block.instructions.empty()) continue;
            for (const auto& successor : candidate_block.instructions.back().successors) {
                const auto target = block_indices.find(successor.block);
                if (target != block_indices.end()) ++predecessor_counts[target->second];
            }
        }
        std::vector<std::vector<std::size_t>> last_use(critical_block_count,
            std::vector<std::size_t>(critical_register_count, 0U));
        std::vector<std::vector<std::uint8_t>> has_use(critical_block_count,
            std::vector<std::uint8_t>(critical_register_count, 0U));
        for (std::size_t use_block_index = 0; use_block_index < critical_block_count; ++use_block_index) {
            const auto& use_block = function.blocks[use_block_index];
            for (std::size_t use_instruction_index = 0; use_instruction_index < use_block.instructions.size(); ++use_instruction_index) {
                const auto& use_instruction = use_block.instructions[use_instruction_index];
                const auto record_use = [&](VirtualRegister reg) {
                    if (reg >= critical_register_count) return;
                    has_use[use_block_index][reg] = 1U;
                    last_use[use_block_index][reg] = use_instruction_index;
                };
                for (const auto reg : use_instruction.inputs) record_use(reg);
                for (const auto& edge : use_instruction.successors)
                    for (const auto reg : edge.arguments) record_use(reg);
            }
        }

        std::unordered_set<std::string> split_successors;
        std::uint32_t edge_serial = 0U;
        for (std::size_t block_index = 0; block_index < critical_block_count; ++block_index) {
            if (function.blocks[block_index].instructions.empty() ||
                function.blocks[block_index].instructions.back().successors.size() != 1U) continue;
            const auto successor_name = function.blocks[block_index].instructions.back().successors.front().block;
            const auto successor_it = block_indices.find(successor_name);
            if (successor_it == block_indices.end() || predecessor_counts[successor_it->second] <= 1U ||
                split_successors.contains(successor_name)) continue;
            const auto successor_index = successor_it->second;

            std::vector<std::size_t> call_sites;
            for (std::size_t instruction_index = 0;
                 instruction_index < function.blocks[block_index].instructions.size(); ++instruction_index)
                if (is_call_opcode(function.blocks[block_index].instructions[instruction_index].opcode))
                    call_sites.push_back(instruction_index);

            for (auto call_it = call_sites.rbegin(); call_it != call_sites.rend(); ++call_it) {
                const auto instruction_index = *call_it;
                const auto call_result = function.blocks[block_index].instructions[instruction_index].result;
                std::vector<VirtualRegister> floating_candidates;
                std::vector<VirtualRegister> integer_candidates;
                for (VirtualRegister reg = 0; reg < critical_register_count; ++reg) {
                    if (!liveness.live_after[block_index][instruction_index][reg] ||
                        !liveness.live_out[block_index][reg] || reg == call_result) continue;
                    if (has_use[block_index][reg] && last_use[block_index][reg] > instruction_index) continue;
                    if (!has_use[successor_index][reg]) continue;
                    const bool floating = reg < function.register_classes.size() &&
                                          uses_vector_register_file(function.register_classes[reg]);
                    (floating ? floating_candidates : integer_candidates).push_back(reg);
                }

                std::stable_sort(integer_candidates.begin(), integer_candidates.end(), [&](VirtualRegister left, VirtualRegister right) {
                    if (intervals[left].spill_weight != intervals[right].spill_weight)
                        return intervals[left].spill_weight < intervals[right].spill_weight;
                    return left < right;
                });
                std::vector<VirtualRegister> selected = floating_candidates;
                if (integer_candidates.size() > 2U)
                    selected.insert(selected.end(), integer_candidates.begin(), integer_candidates.end() - 2);
                if (selected.empty()) continue;

                std::vector<Instruction> stores;
                std::vector<Instruction> reloads;
                std::vector<std::pair<VirtualRegister, VirtualRegister>> merge_replacements;
                std::vector<VirtualRegister> reloaded_values;
                for (const auto reg : selected) {
                    const auto width = reg < function.register_widths.size() ? function.register_widths[reg] : std::uint8_t{64};
                    const auto register_class = reg < function.register_classes.size()
                        ? function.register_classes[reg] : RegisterClass::integer;
                    function.local_stack_size += spill_slot_bytes(register_class, width);
                    const auto offset = -static_cast<std::int64_t>(function.local_stack_size);
                    stores.push_back({split_store_opcode(register_class, width), 0U, {reg}, offset, 0U, {}, {}});

                    const auto reloaded = function.register_count++;
                    function.register_widths.push_back(width);
                    function.register_classes.push_back(register_class);
                    reloads.push_back({split_load_opcode(register_class, width), reloaded, {}, offset, 0U, {}, {}});
                    reloaded_values.push_back(reloaded);

                    const auto merged = function.register_count++;
                    function.register_widths.push_back(width);
                    function.register_classes.push_back(register_class);
                    merge_replacements.emplace_back(reg, merged);
                    function.blocks[successor_index].parameters.push_back(merged);

                    ++stats.split_values;
                    ++stats.cross_block_split_values;
                    ++stats.critical_edge_split_values;
                    ++stats.merge_parameters;
                    ++stats.transition_stores;
                    ++stats.transition_loads;
                    stats.transition_bytes += 16U;
                }

                auto& source_block = function.blocks[block_index];
                source_block.instructions.insert(
                    source_block.instructions.begin() + static_cast<std::ptrdiff_t>(instruction_index),
                    stores.begin(), stores.end());

                const auto edge_name = source_block.name + ".split." + std::to_string(edge_serial++);
                auto& redirected_edge = source_block.instructions.back().successors.front();
                auto original_edge_arguments = std::move(redirected_edge.arguments);
                redirected_edge.block = edge_name;
                redirected_edge.arguments.clear();

                // All existing incoming edges to the merge block pass the
                // original values. The newly-created split edge passes reloads.
                for (auto& incoming_block : function.blocks) {
                    if (incoming_block.instructions.empty()) continue;
                    for (auto& incoming : incoming_block.instructions.back().successors) {
                        if (incoming.block != successor_name) continue;
                        for (const auto reg : selected) incoming.arguments.push_back(reg);
                    }
                }

                for (const auto& [from, to] : merge_replacements) {
                    for (auto& instruction : function.blocks[successor_index].instructions)
                        rewrite_register(instruction, from, to);
                }

                Block edge_block;
                edge_block.name = edge_name;
                edge_block.instructions = std::move(reloads);
                Instruction jump{Opcode::jump, 0U, {}, 0, 0U, {}, {}};
                original_edge_arguments.insert(original_edge_arguments.end(),
                                               reloaded_values.begin(), reloaded_values.end());
                jump.successors.push_back({successor_name, std::move(original_edge_arguments)});
                edge_block.instructions.push_back(std::move(jump));
                function.blocks.push_back(std::move(edge_block));
                ++stats.critical_edge_blocks;
                split_successors.insert(successor_name);
                break;
            }
        }
    }

    return stats;
}

std::vector<LiveInterval> compute_live_intervals(const Function& function, const LivenessAnalysis& liveness) {
    std::vector<LiveInterval> intervals(function.register_count);
    for (VirtualRegister reg = 0; reg < function.register_count; ++reg)
        intervals[reg] = {reg, undefined_position, 0, 0, 0, 0, 0, {}};

    const auto block_count = function.blocks.size();
    const auto& live_in = liveness.live_in;
    const auto& live_out = liveness.live_out;
    const auto& successors = liveness.successors;
    std::vector<std::uint32_t> block_starts(block_count);
    std::vector<std::uint32_t> block_ends(block_count);

    // Instruction positions depend only on block sizes. Establish boundaries
    // without walking every instruction, then make one metadata pass after loop
    // depth is known. The old implementation scanned instructions once to build
    // ranges/use counts and again to rebuild weighted use costs.
    std::uint32_t position = 0;
    for (std::size_t block_index = 0; block_index < block_count; ++block_index) {
        block_starts[block_index] = position;
        ++position; // block-parameter position
        position += static_cast<std::uint32_t>(function.blocks[block_index].instructions.size());
        block_ends[block_index] = position;
    }

    // Approximate natural-loop depth from backward CFG edges. Every block in the
    // target..source range receives one additional nesting level. This is
    // deterministic, inexpensive, and substantially more representative than
    // weighting only the terminator that carries the backedge.
    std::vector<std::uint32_t> block_loop_depth(block_count, 0U);
    for (std::size_t source = 0; source < block_count; ++source) {
        for (const auto target : successors[source]) {
            if (target > source) continue;
            for (std::size_t member = target; member <= source; ++member)
                ++block_loop_depth[member];
        }
    }

    // Reuse segment scratch for every block. Only registers actually touched in
    // the block are reset/emitted, avoiding two register-sized allocations and
    // a full register scan solely to discover empty segment entries per block.
    std::vector<std::uint32_t> first(function.register_count, undefined_position);
    std::vector<std::uint32_t> last(function.register_count, 0U);
    std::vector<VirtualRegister> touched_registers;
    touched_registers.reserve(std::min<std::size_t>(function.register_count, 256U));

    for (std::size_t block_index = 0; block_index < block_count; ++block_index) {
        const auto& block = function.blocks[block_index];
        const auto depth = block_loop_depth[block_index];
        const auto loop_multiplier = depth == 0U ? 1U : (depth == 1U ? 8U : 32U);
        touched_registers.clear();

        const auto mark = [&](VirtualRegister reg, std::uint32_t at) {
            if (reg >= function.register_count) return;
            if (first[reg] == undefined_position) touched_registers.push_back(reg);
            first[reg] = std::min(first[reg], at);
            last[reg] = std::max(last[reg], at);
        };

        // Boundary liveness both extends the global interval and contributes the
        // boundary endpoints of this block's disjoint segment.
        live_in[block_index].for_each_set_bit([&](VirtualRegister reg) {
            touch(intervals, reg, block_starts[block_index]);
            mark(reg, block_starts[block_index]);
        });
        live_out[block_index].for_each_set_bit([&](VirtualRegister reg) {
            touch(intervals, reg, block_ends[block_index]);
            mark(reg, block_ends[block_index]);
        });

        for (const auto parameter : block.parameters) {
            touch(intervals, parameter, block_starts[block_index]);
            mark(parameter, block_starts[block_index]);
        }

        auto at = block_starts[block_index] + 1U;
        for (const auto& instruction : block.instructions) {
            const auto record_use = [&](VirtualRegister reg, std::uint32_t weight) {
                if (reg >= function.register_count) return;
                touch(intervals, reg, at);
                mark(reg, at);
                if (intervals[reg].use_count++ == 0U) intervals[reg].spill_weight = 1U;
                intervals[reg].spill_weight += weight * loop_multiplier;
                intervals[reg].loop_depth = std::max(intervals[reg].loop_depth, depth);
            };
            for (const auto input : instruction.inputs) record_use(input, 2U);
            for (const auto& successor : instruction.successors)
                for (const auto argument : successor.arguments) record_use(argument, 4U);
            if (produces_result(instruction.opcode) && instruction.result < function.register_count) {
                touch(intervals, instruction.result, at);
                mark(instruction.result, at);
            }
            ++at;
        }

        for (const auto reg : touched_registers) {
            intervals[reg].segments.push_back({first[reg], last[reg]});
            first[reg] = undefined_position;
            last[reg] = 0U;
        }
    }

    for (auto& interval : intervals) {
        if (interval.start == undefined_position) interval.start = interval.end = 0;
    }
    return intervals;
}

std::vector<LiveInterval> compute_live_intervals(const Function& function) {
    const auto liveness = analyze_liveness(function, false);
    return compute_live_intervals(function, liveness);
}

namespace {
std::uint64_t spill_priority(const LiveInterval& interval) {
    const auto length = static_cast<std::uint64_t>(interval.end - interval.start + 1U);
    return (static_cast<std::uint64_t>(interval.spill_weight) + 1U) * 1024U / length;
}

template <typename Active>
bool should_spill_active(const Active& active, const LiveInterval& incoming) {
    const auto active_priority = spill_priority(active.interval);
    const auto incoming_priority = spill_priority(incoming);
    if (active_priority != incoming_priority) return active_priority < incoming_priority;
    return active.interval.end > incoming.end;
}

bool segments_overlap(const LiveInterval& left, const LiveInterval& right) {
    for (const auto& lhs : left.segments)
        for (const auto& rhs : right.segments)
            if (lhs.start <= rhs.end && rhs.start <= lhs.end) return true;
    return false;
}
} // namespace

RegisterAllocation allocate_linear_scan(const Function& function) {
    RegisterAllocation allocation;
    if (function.register_count > 16384U) {
        allocation.diagnostics.push_back({DiagnosticSeverity::error,
            "linear-scan virtual-register limit exceeded in @" + function.name, {}});
        return allocation;
    }

    allocation.intervals = compute_live_intervals(function);
    allocation.locations.resize(function.register_count);

    std::vector<std::uint32_t> call_positions;
    std::vector<bool> forced_floating_spill(function.register_count, false);
    std::vector<VirtualRegister> copy_sources(function.register_count, function.register_count);
    // Treat loop-backedge block arguments as copy affinities. A loop-carried
    // value and the corresponding header parameter are not simultaneously live
    // across the edge, so assigning them the same physical register removes the
    // edge parallel copy entirely. Prefer backward edges because entry edges
    // often carry constants while the backedge represents the steady-state hot
    // path.
    std::unordered_map<std::string, std::size_t> allocation_block_indices;
    for (std::size_t block_index = 0; block_index < function.blocks.size(); ++block_index)
        allocation_block_indices.emplace(function.blocks[block_index].name, block_index);
    for (std::size_t source_index = 0; source_index < function.blocks.size(); ++source_index) {
        const auto& source_block = function.blocks[source_index];
        if (source_block.instructions.empty()) continue;
        for (const auto& successor : source_block.instructions.back().successors) {
            const auto target = allocation_block_indices.find(successor.block);
            if (target == allocation_block_indices.end() || target->second > source_index) continue;
            const auto& parameters = function.blocks[target->second].parameters;
            const auto count = std::min(parameters.size(), successor.arguments.size());
            for (std::size_t index = 0; index < count; ++index) {
                const auto parameter = parameters[index];
                const auto argument = successor.arguments[index];
                if (parameter >= function.register_count || argument >= function.register_count || parameter == argument)
                    continue;
                const auto parameter_class = parameter < function.register_classes.size()
                    ? function.register_classes[parameter] : RegisterClass::integer;
                const auto argument_class = argument < function.register_classes.size()
                    ? function.register_classes[argument] : RegisterClass::integer;
                if (parameter_class != argument_class) continue;
                copy_sources[parameter] = argument;
                ++allocation.copy_hint_count;
            }
        }
    }

    std::vector<VirtualRegister> two_address_sources(function.register_count, function.register_count);
    std::vector<VirtualRegister> unary_sources(function.register_count, function.register_count);
    std::uint32_t position = 0;
    for (const auto& block : function.blocks) {
        ++position;
        for (const auto& instruction : block.instructions) {
            if ((instruction.opcode == Opcode::copy || instruction.opcode == Opcode::copy_f32 ||
                 instruction.opcode == Opcode::copy_f64) && instruction.inputs.size() == 1 &&
                instruction.result < function.register_count)
                copy_sources[instruction.result] = instruction.inputs.front(), ++allocation.copy_hint_count;
            if (supports_two_address_reuse(instruction.opcode) && !instruction.inputs.empty() &&
                instruction.result < function.register_count) {
                // Immediate arithmetic carries only its register operand in
                // inputs.  It is still a two-address x86 operation and should
                // inherit that dying operand's location just like reg-reg
                // arithmetic.  Missing this case forced add-immediate loop
                // updates through a temporary register followed by an edge
                // copy back into the header parameter.
                auto source = instruction.inputs.front();
                if (instruction.inputs.size() == 2 && is_commutative_two_address(instruction.opcode)) {
                    const auto right = instruction.inputs[1];
                    const auto left_dies = source < allocation.intervals.size() &&
                                           allocation.intervals[source].end == position;
                    const auto right_dies = right < allocation.intervals.size() &&
                                            allocation.intervals[right].end == position;
                    if (!left_dies && right_dies) source = right;
                }
                two_address_sources[instruction.result] = source;
            }
            if (supports_unary_reuse(instruction.opcode) && instruction.inputs.size() == 1 &&
                instruction.result < function.register_count)
                unary_sources[instruction.result] = instruction.inputs.front();
            switch (instruction.opcode) {
            case Opcode::call_i32: case Opcode::call_i64: case Opcode::call_f32: case Opcode::call_f64: case Opcode::call_void: case Opcode::call_aggregate:
            case Opcode::call_indirect_i32: case Opcode::call_indirect_i64: case Opcode::call_indirect_f32:
            case Opcode::call_indirect_f64: case Opcode::call_indirect_void:
                call_positions.push_back(position);
                // Outgoing XMM arguments are placed by the cycle-safe parallel-copy
                // planner.  A floating value consumed by this call does not need a
                // stack home merely because it is an argument; only values whose
                // live interval genuinely crosses the call are required to spill.
                break;
            default: break;
            }
            // Floating entry arguments are resolved as a parallel copy by the
            // x86-64 encoder.  They no longer need to be forced to stack solely
            // to protect incoming XMM registers from sequential load ordering.
            ++position;
        }
    }
    // call_positions is naturally sorted by instruction order.  Count call
    // crossings with binary searches instead of rescanning every call for
    // every interval (and then rescanning them again in crosses_call).
    std::vector<std::uint8_t> interval_crosses_call(allocation.intervals.size(), 0U);
    for (auto& interval : allocation.intervals) {
        const auto first = std::upper_bound(call_positions.begin(), call_positions.end(), interval.start);
        const auto last = std::lower_bound(first, call_positions.end(), interval.end);
        interval.call_crossing_count = static_cast<std::uint32_t>(std::distance(first, last));
        if (interval.call_crossing_count != 0U) {
            ++allocation.call_crossing_interval_count;
            if (interval.virtual_register < interval_crosses_call.size())
                interval_crosses_call[interval.virtual_register] = 1U;
        }
    }
    const auto crosses_call = [&](const LiveInterval& interval) {
        return interval.virtual_register < interval_crosses_call.size() &&
               interval_crosses_call[interval.virtual_register] != 0U;
    };

    // Measure pressure from disjoint liveness segments rather than bounding
    // intervals. Interleaved blocks on mutually exclusive CFG paths therefore
    // no longer inflate pressure or force unnecessary spills.
    for (const auto& interval : allocation.intervals) {
        if (interval.segments.size() > 1U) {
            ++allocation.segmented_interval_count;
            allocation.live_range_hole_count += static_cast<std::uint32_t>(interval.segments.size() - 1U);
        }
    }
    // Compute interference and peak pressure from liveness-segment events.
    // The previous implementation compared every pair of virtual registers and
    // then rescanned every interval at every instruction position.  Those two
    // diagnostic/statistics passes were quadratic and dominated compiler object
    // emission once the compiler grew to thousands of SSA values per function.
    struct LivenessEvent {
        std::uint32_t position{};
        VirtualRegister reg{};
        bool floating{};
        bool start{};
    };
    std::vector<LivenessEvent> events;
    for (const auto& interval : allocation.intervals) {
        if (interval.use_count == 0U) continue;
        const bool floating = interval.virtual_register < function.register_classes.size() &&
                              uses_vector_register_file(function.register_classes[interval.virtual_register]);
        for (const auto& segment : interval.segments) {
            events.push_back({segment.start, interval.virtual_register, floating, true});
            events.push_back({segment.end, interval.virtual_register, floating, false});
        }
    }

    std::stable_sort(events.begin(), events.end(), [](const LivenessEvent& left, const LivenessEvent& right) {
        if (left.position != right.position) return left.position < right.position;
        // Segments are inclusive, so starts at a position overlap values whose
        // segment ends at that same position.
        if (left.start != right.start) return left.start && !right.start;
        return left.reg < right.reg;
    });
    std::unordered_set<VirtualRegister> active_integer;
    std::unordered_set<VirtualRegister> active_floating;
    std::unordered_set<std::uint64_t> interference_edges;
    interference_edges.reserve(events.size());
    for (const auto& event : events) {
        auto& active = event.floating ? active_floating : active_integer;
        if (event.start) {
            for (const auto other : active) {
                if (other == event.reg) continue;
                const auto low = std::min(other, event.reg);
                const auto high = std::max(other, event.reg);
                interference_edges.insert((static_cast<std::uint64_t>(low) << 32U) | high);
            }
            active.insert(event.reg);
            allocation.peak_integer_pressure = std::max(
                allocation.peak_integer_pressure, static_cast<std::uint32_t>(active_integer.size()));
            allocation.peak_floating_pressure = std::max(
                allocation.peak_floating_pressure, static_cast<std::uint32_t>(active_floating.size()));
        } else {
            active.erase(event.reg);
        }
    }
    allocation.interference_edge_count = static_cast<std::uint32_t>(interference_edges.size());

    std::vector<LiveInterval> ordered = allocation.intervals;
    std::stable_sort(ordered.begin(), ordered.end(), [](const LiveInterval& left, const LiveInterval& right) {
        if (left.start != right.start) return left.start < right.start;
        return left.virtual_register < right.virtual_register;
    });

    struct IntegerActive { LiveInterval interval; PhysicalRegister physical; };
    struct FloatingActive { LiveInterval interval; FloatingPhysicalRegister physical; };
    std::vector<IntegerActive> integer_active;
    std::vector<FloatingActive> floating_active;
    constexpr std::array base_integer_physicals{PhysicalRegister::r10d, PhysicalRegister::r11d,
                                                    PhysicalRegister::r12d, PhysicalRegister::r13d,
                                                    PhysicalRegister::r14d, PhysicalRegister::r15d,
                                                    PhysicalRegister::ebx};
    // R8/R9 are reserved by the x86-64 encoder as the integer spill-cache
    // registers.  They must never also be assigned to live virtual registers:
    // a cache fill would silently overwrite the allocated value.  Incoming ABI
    // arguments in R8/R9 are captured by the encoder before allocation use, but
    // that does not make the registers safe for general allocation while the
    // spill cache is active.
    std::vector<PhysicalRegister> integer_physicals(base_integer_physicals.begin(), base_integer_physicals.end());
    constexpr std::array floating_physicals{FloatingPhysicalRegister::xmm2, FloatingPhysicalRegister::xmm3, FloatingPhysicalRegister::xmm4, FloatingPhysicalRegister::xmm5};
    std::vector<PhysicalRegister> free_integer(integer_physicals.begin(), integer_physicals.end());
    std::vector<FloatingPhysicalRegister> free_floating(floating_physicals.begin(), floating_physicals.end());
    const auto take_integer_register = [&](bool across_call) -> std::optional<PhysicalRegister> {
        const auto preferred = std::find_if(free_integer.begin(), free_integer.end(), [&](PhysicalRegister reg) {
            return across_call ? is_callee_saved(reg) : is_call_clobbered(reg);
        });
        if (preferred != free_integer.end()) {
            const auto physical = *preferred;
            free_integer.erase(preferred);
            return physical;
        }
        if (across_call) return std::nullopt;
        if (free_integer.empty()) return std::nullopt;
        const auto physical = free_integer.front();
        free_integer.erase(free_integer.begin());
        return physical;
    };
    const auto record_integer_allocation = [&](PhysicalRegister physical) {
        if (is_callee_saved(physical)) ++allocation.callee_saved_allocation_count;
        else ++allocation.caller_saved_allocation_count;
    };
    auto spill = [&](VirtualRegister reg) {
        allocation.locations[reg] = {LocationKind::stack_slot, PhysicalRegister::r10d,
                                     FloatingPhysicalRegister::xmm2, 0};
        ++allocation.spill_count;
    };

    for (const auto& interval : ordered) {
        const bool floating = interval.virtual_register < function.register_classes.size() &&
                              uses_vector_register_file(function.register_classes[interval.virtual_register]);
        if (floating) {
            for (auto iterator = floating_active.begin(); iterator != floating_active.end();) {
                if (iterator->interval.end < interval.start) {
                    free_floating.push_back(iterator->physical);
                    iterator = floating_active.erase(iterator);
                } else ++iterator;
            }
            bool coalesced = false;
            const auto copy_source = copy_sources[interval.virtual_register];
            const auto arithmetic_source = two_address_sources[interval.virtual_register];
            const auto unary_source = unary_sources[interval.virtual_register];
            const auto source = copy_source < function.register_count ? copy_source :
                                arithmetic_source < function.register_count ? arithmetic_source : unary_source;
            if (!crosses_call(interval) && !forced_floating_spill[interval.virtual_register] &&
                source < function.register_count) {
                const auto source_active = std::find_if(floating_active.begin(), floating_active.end(),
                    [&](const FloatingActive& active) {
                        return active.interval.virtual_register == source && active.interval.end == interval.start;
                    });
                if (source_active != floating_active.end()) {
                    const auto physical = source_active->physical;
                    floating_active.erase(source_active);
                    allocation.locations[interval.virtual_register] = {
                        LocationKind::floating_register, PhysicalRegister::r10d, physical, 0};
                    floating_active.push_back({interval, physical});
                    ++allocation.physical_count;
                    if (copy_source < function.register_count) ++allocation.coalesced_copy_count;
                    else if (arithmetic_source < function.register_count) ++allocation.two_address_reuse_count;
                    else ++allocation.unary_reuse_count;
                    coalesced = true;
                }
            }
            if (coalesced) {
                std::sort(floating_active.begin(), floating_active.end(),
                    [](const FloatingActive& left, const FloatingActive& right) { return left.interval.end < right.interval.end; });
                continue;
            }
            if (crosses_call(interval) || forced_floating_spill[interval.virtual_register]) {
                spill(interval.virtual_register);
            } else if (!free_floating.empty()) {
                const auto physical = free_floating.back();
                free_floating.pop_back();
                allocation.locations[interval.virtual_register] = {
                    LocationKind::floating_register, PhysicalRegister::r10d, physical, 0};
                floating_active.push_back({interval, physical});
                ++allocation.physical_count;
            } else {
                auto candidate = std::min_element(floating_active.begin(), floating_active.end(),
                    [](const FloatingActive& left, const FloatingActive& right) {
                        const auto left_priority = spill_priority(left.interval);
                        const auto right_priority = spill_priority(right.interval);
                        if (left_priority != right_priority) return left_priority < right_priority;
                        return left.interval.end > right.interval.end;
                    });
                if (candidate != floating_active.end() && should_spill_active(*candidate, interval)) {
                    const auto physical = candidate->physical;
                    spill(candidate->interval.virtual_register);
                    ++allocation.weighted_spill_decision_count;
                    *candidate = {interval, physical};
                    allocation.locations[interval.virtual_register] = {
                        LocationKind::floating_register, PhysicalRegister::r10d, physical, 0};
                    ++allocation.physical_count;
                } else spill(interval.virtual_register);
            }
            std::sort(floating_active.begin(), floating_active.end(),
                [](const FloatingActive& left, const FloatingActive& right) { return left.interval.end < right.interval.end; });
            continue;
        }

        for (auto iterator = integer_active.begin(); iterator != integer_active.end();) {
            if (iterator->interval.end < interval.start) {
                free_integer.push_back(iterator->physical);
                iterator = integer_active.erase(iterator);
            } else ++iterator;
        }
        bool coalesced = false;
        const auto copy_source = copy_sources[interval.virtual_register];
        const auto arithmetic_source = two_address_sources[interval.virtual_register];
        const auto unary_source = unary_sources[interval.virtual_register];
        const auto source = copy_source < function.register_count ? copy_source :
                            arithmetic_source < function.register_count ? arithmetic_source : unary_source;
        if (source < function.register_count) {
            const auto source_active = std::find_if(integer_active.begin(), integer_active.end(),
                [&](const IntegerActive& active) {
                    return active.interval.virtual_register == source && active.interval.end == interval.start;
                });
            if (source_active != integer_active.end() &&
                (!crosses_call(interval) || is_callee_saved(source_active->physical))) {
                const auto physical = source_active->physical;
                integer_active.erase(source_active);
                allocation.locations[interval.virtual_register] = {
                    LocationKind::physical_register, physical, FloatingPhysicalRegister::xmm2, 0};
                integer_active.push_back({interval, physical});
                ++allocation.physical_count;
                record_integer_allocation(physical);
                if (copy_source < function.register_count) ++allocation.coalesced_copy_count;
                else if (arithmetic_source < function.register_count) ++allocation.two_address_reuse_count;
                else ++allocation.unary_reuse_count;
                coalesced = true;
            }
        }
        if (coalesced) {
            std::sort(integer_active.begin(), integer_active.end(),
                [](const IntegerActive& left, const IntegerActive& right) { return left.interval.end < right.interval.end; });
            continue;
        }
        const bool interval_crosses_call = crosses_call(interval);
        if (const auto selected = take_integer_register(interval_crosses_call)) {
            const auto physical = *selected;
            allocation.locations[interval.virtual_register] = {
                LocationKind::physical_register, physical, FloatingPhysicalRegister::xmm2, 0};
            integer_active.push_back({interval, physical});
            ++allocation.physical_count;
            record_integer_allocation(physical);
        } else {
            auto candidate = std::min_element(integer_active.begin(), integer_active.end(),
                [&](const IntegerActive& left, const IntegerActive& right) {
                    const bool left_eligible = !interval_crosses_call || is_callee_saved(left.physical);
                    const bool right_eligible = !interval_crosses_call || is_callee_saved(right.physical);
                    if (left_eligible != right_eligible) return left_eligible;
                    const auto left_priority = spill_priority(left.interval);
                    const auto right_priority = spill_priority(right.interval);
                    if (left_priority != right_priority) return left_priority < right_priority;
                    return left.interval.end > right.interval.end;
                });
            if (candidate != integer_active.end() &&
                (!interval_crosses_call || is_callee_saved(candidate->physical)) &&
                should_spill_active(*candidate, interval)) {
                const auto physical = candidate->physical;
                spill(candidate->interval.virtual_register);
                ++allocation.weighted_spill_decision_count;
                *candidate = {interval, physical};
                allocation.locations[interval.virtual_register] = {
                    LocationKind::physical_register, physical, FloatingPhysicalRegister::xmm2, 0};
                ++allocation.physical_count;
                record_integer_allocation(physical);
            } else spill(interval.virtual_register);
        }
        std::sort(integer_active.begin(), integer_active.end(),
            [](const IntegerActive& left, const IntegerActive& right) { return left.interval.end < right.interval.end; });
    }

    // Recover registers for spills caused only by bounding-range overlap. A
    // physical register can be shared when no already allocated value of the
    // same class interferes in any real liveness segment. This is the first
    // segmented-allocation stage and requires no mid-interval location change.
    const auto integer_available_except_two = [&](VirtualRegister reg, PhysicalRegister physical,
                                                  VirtualRegister ignored, VirtualRegister also_ignored) {
        for (VirtualRegister other = 0; other < function.register_count; ++other) {
            if (other == reg || other == ignored || other == also_ignored) continue;
            const auto& location = allocation.locations[other];
            if (location.kind != LocationKind::physical_register || location.physical != physical) continue;
            if (segments_overlap(allocation.intervals[reg], allocation.intervals[other])) return false;
        }
        return true;
    };
    const auto integer_available_except = [&](VirtualRegister reg, PhysicalRegister physical, VirtualRegister ignored) {
        return integer_available_except_two(reg, physical, ignored, function.register_count);
    };
    const auto integer_available = [&](VirtualRegister reg, PhysicalRegister physical) {
        return integer_available_except(reg, physical, function.register_count);
    };
    const auto floating_available_except = [&](VirtualRegister reg, FloatingPhysicalRegister physical, VirtualRegister ignored) {
        for (VirtualRegister other = 0; other < function.register_count; ++other) {
            if (other == reg || other == ignored) continue;
            const auto& location = allocation.locations[other];
            if (location.kind != LocationKind::floating_register || location.floating != physical) continue;
            if (segments_overlap(allocation.intervals[reg], allocation.intervals[other])) return false;
        }
        return true;
    };
    const auto floating_available = [&](VirtualRegister reg, FloatingPhysicalRegister physical) {
        return floating_available_except(reg, physical, function.register_count);
    };
    for (VirtualRegister reg = 0; reg < function.register_count; ++reg) {
        if (allocation.locations[reg].kind != LocationKind::stack_slot || allocation.intervals[reg].use_count == 0U)
            continue;
        const bool floating = reg < function.register_classes.size() &&
                              uses_vector_register_file(function.register_classes[reg]);
        bool recovered = false;
        if (floating) {
            if (!crosses_call(allocation.intervals[reg]) && !forced_floating_spill[reg]) {
                for (const auto physical : floating_physicals) {
                    if (!floating_available(reg, physical)) continue;
                    allocation.locations[reg] = {LocationKind::floating_register, PhysicalRegister::r10d, physical, 0};
                    recovered = true;
                    break;
                }
            }
        } else {
            for (const auto physical : integer_physicals) {
                if (crosses_call(allocation.intervals[reg]) && !is_callee_saved(physical)) continue;
                if (!integer_available(reg, physical)) continue;
                allocation.locations[reg] = {LocationKind::physical_register, physical, FloatingPhysicalRegister::xmm2, 0};
                record_integer_allocation(physical);
                recovered = true;
                break;
            }
        }
        if (recovered) {
            --allocation.spill_count;
            ++allocation.physical_count;
            ++allocation.hole_aware_register_reuse_count;
        }
    }


    // Revisit two-address and unary reuse after the initial scan.  Block
    // layout and segmented liveness can make a dying source unavailable to
    // the local scan even though the final allocation has no real conflict.
    // Relocating the short-lived result into the source register is especially
    // important for loop induction updates: it lets an add-immediate update
    // the loop-carried register directly and removes the backedge copy.
    for (VirtualRegister destination = 0; destination < function.register_count; ++destination) {
        const auto arithmetic_source = two_address_sources[destination];
        const auto unary_source = unary_sources[destination];
        const auto source = arithmetic_source < function.register_count ? arithmetic_source : unary_source;
        if (source >= function.register_count || source == destination) continue;
        if (allocation.intervals[destination].use_count == 0U || allocation.intervals[source].use_count == 0U)
            continue;
        if (allocation.intervals[source].end != allocation.intervals[destination].start &&
            segments_overlap(allocation.intervals[source], allocation.intervals[destination]))
            continue;
        const auto destination_class = destination < function.register_classes.size()
            ? function.register_classes[destination] : RegisterClass::integer;
        const auto source_class = source < function.register_classes.size()
            ? function.register_classes[source] : RegisterClass::integer;
        if (destination_class != source_class) continue;
        const bool floating = uses_vector_register_file(destination_class);
        auto& destination_location = allocation.locations[destination];
        const auto& source_location = allocation.locations[source];
        if (floating && source_location.kind == LocationKind::floating_register &&
            !crosses_call(allocation.intervals[destination]) && !forced_floating_spill[destination] &&
            floating_available_except(destination, source_location.floating, source)) {
            if (destination_location.kind == LocationKind::floating_register &&
                destination_location.floating == source_location.floating) continue;
            destination_location = {LocationKind::floating_register, PhysicalRegister::r10d,
                                    source_location.floating, 0};
            if (arithmetic_source < function.register_count) ++allocation.two_address_reuse_count;
            else ++allocation.unary_reuse_count;
        } else if (!floating && source_location.kind == LocationKind::physical_register &&
                   (!crosses_call(allocation.intervals[destination]) || is_callee_saved(source_location.physical))) {
            // A loop induction update commonly has three consecutive virtual
            // values that should occupy one physical register: the body
            // parameter, the arithmetic result, and the successor header
            // parameter.  The successor parameter can appear to interfere with
            // the arithmetic result even though the edge copy defines it only
            // after the result's final use. Ignore that single affinity consumer
            // while relocating the short-lived result into the dying source.
            VirtualRegister affinity_consumer = function.register_count;
            for (const auto& predecessor : function.blocks) {
                if (predecessor.instructions.empty()) continue;
                for (const auto& edge : predecessor.instructions.back().successors) {
                    const auto target = allocation_block_indices.find(edge.block);
                    if (target == allocation_block_indices.end()) continue;
                    const auto& parameters = function.blocks[target->second].parameters;
                    const auto count = std::min(parameters.size(), edge.arguments.size());
                    for (std::size_t index = 0; index < count; ++index) {
                        if (edge.arguments[index] == destination) {
                            affinity_consumer = parameters[index];
                            break;
                        }
                    }
                    if (affinity_consumer < function.register_count) break;
                }
                if (affinity_consumer < function.register_count) break;
            }
            const auto feeds_affinity_consumer = [&](VirtualRegister candidate) {
                if (affinity_consumer >= function.register_count) return false;
                for (const auto& predecessor : function.blocks) {
                    if (predecessor.instructions.empty()) continue;
                    for (const auto& edge : predecessor.instructions.back().successors) {
                        const auto target = allocation_block_indices.find(edge.block);
                        if (target == allocation_block_indices.end()) continue;
                        const auto& parameters = function.blocks[target->second].parameters;
                        const auto count = std::min(parameters.size(), edge.arguments.size());
                        for (std::size_t index = 0; index < count; ++index)
                            if (parameters[index] == affinity_consumer && edge.arguments[index] == candidate)
                                return true;
                    }
                }
                return false;
            };
            bool available = true;
            for (VirtualRegister other = 0; other < function.register_count; ++other) {
                if (other == destination || other == source || other == affinity_consumer ||
                    feeds_affinity_consumer(other)) continue;
                const auto& location = allocation.locations[other];
                if (location.kind != LocationKind::physical_register ||
                    location.physical != source_location.physical) continue;
                if (segments_overlap(allocation.intervals[destination], allocation.intervals[other])) {
                    available = false;
                    break;
                }
            }
            if (!available) continue;
            if (destination_location.kind == LocationKind::physical_register &&
                destination_location.physical == source_location.physical) continue;
            destination_location = {LocationKind::physical_register, source_location.physical,
                                    FloatingPhysicalRegister::xmm2, 0};
            if (arithmetic_source < function.register_count) ++allocation.two_address_reuse_count;
            else ++allocation.unary_reuse_count;
        }
    }

    // Destructively coalesce simple induction-variable phi webs.  After hot-loop
    // layout the physical backedge may be forward in block order, so the normal
    // backward-edge affinity heuristic cannot see it.  When an arithmetic result
    // is used solely as an edge argument, its source dies at the definition, and
    // every feeder of the target parameter is mutually exclusive, the complete
    // source/result/parameter web can safely share one physical register.
    for (VirtualRegister result = 0; result < function.register_count; ++result) {
        const auto source = two_address_sources[result];
        if (source >= function.register_count || source == result ||
            allocation.intervals[result].use_count != 1U ||
            allocation.intervals[source].end != allocation.intervals[result].start)
            continue;
        const auto source_location = allocation.locations[source];
        if (source_location.kind != LocationKind::physical_register) continue;

        // The destructive source must itself be a loop-carried block parameter.
        // A dying literal or temporary operand is not part of the induction phi
        // web even when a two-address instruction could legally overwrite it.
        // Coalescing such an operand into the destination parameter can corrupt
        // another recurrence that still consumes the value (Fibonacci exposed
        // this with the shared constant one).
        std::optional<std::size_t> source_parameter_index;
        const machine::Block* defining_block = nullptr;
        for (const auto& candidate_block : function.blocks) {
            if (std::any_of(candidate_block.instructions.begin(), candidate_block.instructions.end(),
                            [&](const auto& instruction) { return instruction.result == result; })) {
                defining_block = &candidate_block;
                const auto source_it = std::find(candidate_block.parameters.begin(),
                                                 candidate_block.parameters.end(), source);
                if (source_it != candidate_block.parameters.end())
                    source_parameter_index = static_cast<std::size_t>(
                        std::distance(candidate_block.parameters.begin(), source_it));
                break;
            }
        }
        if (!defining_block || !source_parameter_index) continue;

        VirtualRegister parameter = function.register_count;
        std::optional<std::size_t> target_parameter_index;
        std::vector<VirtualRegister> feeders;
        for (const auto& predecessor : function.blocks) {
            if (predecessor.instructions.empty()) continue;
            for (const auto& edge : predecessor.instructions.back().successors) {
                const auto target = allocation_block_indices.find(edge.block);
                if (target == allocation_block_indices.end()) continue;
                const auto& parameters = function.blocks[target->second].parameters;
                const auto count = std::min(parameters.size(), edge.arguments.size());
                for (std::size_t index = 0; index < count; ++index) {
                    if (edge.arguments[index] == result) {
                        parameter = parameters[index];
                        target_parameter_index = index;
                    }
                }
            }
        }
        if (parameter >= function.register_count || !target_parameter_index ||
            *target_parameter_index != *source_parameter_index)
            continue;
        // Only the steady-state induction chain may be destructively
        // coalesced. Other predecessor feeders (typically entry constants) can
        // have unrelated uses elsewhere in the loop. Assigning those feeders
        // to the induction register can silently overwrite a still-live value
        // (for example Fibonacci's constant one, which also feeds the counter
        // decrement). Entry edges can retain an ordinary parallel copy; the hot
        // backedge is the part that must become copy-free.
        feeders.push_back(source);
        feeders.push_back(result);
        feeders.push_back(parameter);
        std::sort(feeders.begin(), feeders.end());
        feeders.erase(std::unique(feeders.begin(), feeders.end()), feeders.end());

        bool safe = true;
        for (VirtualRegister other = 0; other < function.register_count && safe; ++other) {
            if (std::binary_search(feeders.begin(), feeders.end(), other)) continue;
            const auto& location = allocation.locations[other];
            if (location.kind == LocationKind::physical_register &&
                location.physical == source_location.physical &&
                segments_overlap(allocation.intervals[result], allocation.intervals[other]))
                safe = false;
        }
        if (!safe) continue;
        for (const auto member : feeders) {
            if (member >= function.register_count) continue;
            const bool floating = member < function.register_classes.size() &&
                                  uses_vector_register_file(function.register_classes[member]);
            if (floating) { safe = false; break; }
        }
        if (!safe) continue;
        for (const auto member : feeders)
            allocation.locations[member] = {LocationKind::physical_register, source_location.physical,
                                            FloatingPhysicalRegister::xmm2, 0};
        ++allocation.two_address_reuse_count;
    }

    // Honor copy affinities after the initial scan and hole-aware recovery.
    // Local coalescing above handles adjacent intervals. This global stage uses
    // segmented interference, so copies that cross block boundaries or
    // liveness holes can still share the source location when no real live
    // segment conflicts with that physical register.
    const auto copy_segments_conflict = [](const LiveInterval& source, const LiveInterval& destination) {
        for (const auto& left : source.segments) {
            for (const auto& right : destination.segments) {
                const auto overlap_start = std::max(left.start, right.start);
                const auto overlap_end = std::min(left.end, right.end);
                if (overlap_start < overlap_end) return true;
            }
        }
        return false;
    };
    for (VirtualRegister destination = 0; destination < function.register_count; ++destination) {
        const auto source = copy_sources[destination];
        if (source >= function.register_count || source == destination) continue;
        if (allocation.intervals[destination].use_count == 0U ||
            allocation.intervals[source].use_count == 0U) continue;
        if (copy_segments_conflict(allocation.intervals[source], allocation.intervals[destination])) continue;

        // Coalescing requires the same kind of value, not merely the same
        // register file: a float and a packed value both live in XMM but are
        // not interchangeable.
        const auto destination_class = destination < function.register_classes.size()
            ? function.register_classes[destination] : RegisterClass::integer;
        const auto source_class = source < function.register_classes.size()
            ? function.register_classes[source] : RegisterClass::integer;
        if (destination_class != source_class) continue;
        const bool floating = uses_vector_register_file(destination_class);

        auto& destination_location = allocation.locations[destination];
        auto& source_location = allocation.locations[source];
        if ((floating && destination_location.kind == LocationKind::floating_register &&
             source_location.kind == LocationKind::floating_register &&
             destination_location.floating == source_location.floating) ||
            (!floating && destination_location.kind == LocationKind::physical_register &&
             source_location.kind == LocationKind::physical_register &&
             destination_location.physical == source_location.physical))
            continue;
        const bool destination_was_spilled = destination_location.kind == LocationKind::stack_slot;
        bool coalesced = false;
        if (floating && source_location.kind == LocationKind::floating_register &&
            !crosses_call(allocation.intervals[destination]) && !forced_floating_spill[destination] &&
            floating_available_except(destination, source_location.floating, source)) {
            destination_location = {LocationKind::floating_register, PhysicalRegister::r10d,
                                    source_location.floating, 0};
            coalesced = true;
        } else if (!floating && source_location.kind == LocationKind::physical_register &&
                   (!crosses_call(allocation.intervals[destination]) ||
                    is_callee_saved(source_location.physical)) &&
                   integer_available_except(destination, source_location.physical, source)) {
            destination_location = {LocationKind::physical_register, source_location.physical,
                                    FloatingPhysicalRegister::xmm2, 0};
            if (destination_was_spilled) record_integer_allocation(source_location.physical);
            coalesced = true;
        } else if (floating && destination_location.kind == LocationKind::floating_register &&
                   !crosses_call(allocation.intervals[source]) && !forced_floating_spill[source] &&
                   floating_available_except(source, destination_location.floating, destination)) {
            // Backedge values are commonly defined after their loop-header
            // parameters have already received registers.  Relocate the
            // short-lived producer into the parameter's register when moving
            // the parameter to the producer is blocked by another interval.
            source_location = {LocationKind::floating_register, PhysicalRegister::r10d,
                               destination_location.floating, 0};
            coalesced = true;
        } else if (!floating && destination_location.kind == LocationKind::physical_register &&
                   (!crosses_call(allocation.intervals[source]) ||
                    is_callee_saved(destination_location.physical)) &&
                   integer_available_except(source, destination_location.physical, destination)) {
            source_location = {LocationKind::physical_register, destination_location.physical,
                               FloatingPhysicalRegister::xmm2, 0};
            coalesced = true;
        }
        if (!coalesced) continue;
        ++allocation.global_copy_affinity_count;
        ++allocation.coalesced_copy_count;
        if (destination_was_spilled) {
            --allocation.spill_count;
            ++allocation.physical_count;
            ++allocation.copy_spills_recovered;
        }
    }

    // Replace stack-backed constants with rematerialized locations before
    // assigning physical spill slots. Recreating these values at each use is
    // cheaper than reserving frame storage and emitting store/load traffic.
    std::vector<const Instruction*> definitions(function.register_count, nullptr);
    for (const auto& block : function.blocks) {
        for (const auto& instruction : block.instructions) {
            if (produces_result(instruction.opcode) && instruction.result < function.register_count)
                definitions[instruction.result] = &instruction;
        }
    }

    for (VirtualRegister reg = 0; reg < function.register_count; ++reg) {
        auto& location = allocation.locations[reg];
        if (location.kind != LocationKind::stack_slot || definitions[reg] == nullptr ||
            allocation.intervals[reg].use_count != 1U) continue;
        const auto opcode = definitions[reg]->opcode;
        const bool integer = opcode == Opcode::load_immediate || opcode == Opcode::load_immediate_i64;
        const bool floating = opcode == Opcode::load_immediate_f32 || opcode == Opcode::load_immediate_f64;
        if (!integer && !floating) continue;
        location.kind = integer ? LocationKind::rematerialized_integer : LocationKind::rematerialized_floating;
        location.rematerialized_immediate = definitions[reg]->immediate;
        ++allocation.rematerialized_value_count;
        allocation.rematerialized_use_count += allocation.intervals[reg].use_count;
    }

    struct ReusableSlot { std::uint32_t end{}; std::uint32_t index{}; };
    std::vector<LiveInterval> spilled_intervals;
    spilled_intervals.reserve(allocation.spill_count);
    for (const auto& interval : allocation.intervals)
        if (allocation.locations[interval.virtual_register].kind == LocationKind::stack_slot)
            spilled_intervals.push_back(interval);
    std::stable_sort(spilled_intervals.begin(), spilled_intervals.end(),
        [](const LiveInterval& left, const LiveInterval& right) {
            if (left.start != right.start) return left.start < right.start;
            if (left.end != right.end) return left.end < right.end;
            return left.virtual_register < right.virtual_register;
        });

    // Slots are sized rather than assumed to be eight bytes wide. A packed
    // value needs its full width, and a slot reused by several intervals has to
    // fit the largest of them, so offsets come from a running total of slot
    // sizes instead of a fixed stride.
    const auto slot_bytes_for = [&](VirtualRegister reg) {
        const auto register_class = reg < function.register_classes.size()
            ? function.register_classes[reg] : RegisterClass::integer;
        const auto width = reg < function.register_widths.size()
            ? function.register_widths[reg] : std::uint8_t{8};
        return spill_slot_bytes(register_class, width);
    };

    std::vector<ReusableSlot> active_slots;
    std::vector<std::uint32_t> free_slots;
    std::vector<std::uint32_t> slot_sizes;
    std::vector<std::pair<VirtualRegister, std::uint32_t>> slot_assignment;
    std::uint32_t unreused_bytes = 0;
    for (const auto& interval : spilled_intervals) {
        for (auto iterator = active_slots.begin(); iterator != active_slots.end();) {
            if (iterator->end < interval.start) {
                free_slots.push_back(iterator->index);
                iterator = active_slots.erase(iterator);
            } else ++iterator;
        }
        const auto needed = slot_bytes_for(interval.virtual_register);
        unreused_bytes += needed;
        std::uint32_t slot = 0;
        if (!free_slots.empty()) {
            slot = free_slots.back();
            free_slots.pop_back();
            ++allocation.reused_spill_slot_count;
        } else {
            slot = static_cast<std::uint32_t>(slot_sizes.size());
            slot_sizes.push_back(0U);
        }
        slot_sizes[slot] = std::max(slot_sizes[slot], needed);
        slot_assignment.emplace_back(interval.virtual_register, slot);
        active_slots.push_back({interval.end, slot});
    }

    std::vector<std::uint32_t> slot_end(slot_sizes.size(), 0U);
    std::uint32_t running = function.local_stack_size;
    for (std::size_t slot = 0; slot < slot_sizes.size(); ++slot) {
        running += slot_sizes[slot];
        slot_end[slot] = running;
    }
    for (const auto& [reg, slot] : slot_assignment)
        allocation.locations[reg].stack_offset = -static_cast<std::int32_t>(slot_end[slot]);

    allocation.spill_slot_count = static_cast<std::uint32_t>(slot_sizes.size());
    allocation.frame_size_before_slot_reuse = align_frame(function.local_stack_size + unreused_bytes);
    allocation.frame_size = align_frame(running);
    allocation.frame_bytes_saved = allocation.frame_size_before_slot_reuse - allocation.frame_size;
    return allocation;
}

StackAllocation allocate_stack_slots(const Function& function) {
    StackAllocation allocation;
    if (function.register_count > 16384U) {
        allocation.diagnostics.push_back({DiagnosticSeverity::error,
            "baseline stack allocator virtual-register limit exceeded in @" + function.name, {}});
        return allocation;
    }

    allocation.offsets.reserve(function.register_count);
    for (VirtualRegister reg = 0; reg < function.register_count; ++reg) {
        const auto byte_offset = static_cast<std::uint64_t>(reg + 1U) * 8U;
        allocation.offsets.push_back(-static_cast<std::int32_t>(function.local_stack_size + byte_offset));
    }
    allocation.frame_size = align_frame(function.local_stack_size + static_cast<std::uint32_t>(function.register_count) * 8U);
    return allocation;
}

} // namespace forge::machine
