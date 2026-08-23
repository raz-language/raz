// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/codegen/aarch64/register_allocation.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace forge::codegen::aarch64 {
namespace {

constexpr std::array<std::uint8_t, 10> integer_pool{19, 20, 21, 22, 23, 24, 25, 26, 27, 28};
constexpr std::array<std::uint8_t, 8> floating_pool{8, 9, 10, 11, 12, 13, 14, 15};
// Full-width Q values use caller-saved registers that are deliberately disjoint
// from scalar FP allocation and from the encoder's v0/v1 scratch pair.
constexpr std::array<std::uint8_t, 8> vector_pool{16, 17, 18, 19, 20, 21, 22, 23};

struct Active {
    machine::LiveInterval interval;
    std::uint8_t physical{};
};

enum class ValueBank : std::uint8_t { integer, floating, vector };

bool segments_overlap(const machine::LiveInterval& left, const machine::LiveInterval& right) noexcept {
    for (const auto& lhs : left.segments)
        for (const auto& rhs : right.segments)
            if (lhs.start <= rhs.end && rhs.start <= lhs.end) return true;
    return false;
}

ValueBank value_bank(const machine::Function& function, machine::VirtualRegister reg) noexcept {
    if (reg >= function.register_classes.size()) return ValueBank::integer;
    if (function.register_classes[reg] == machine::RegisterClass::vector) return ValueBank::vector;
    if (function.register_classes[reg] == machine::RegisterClass::floating) return ValueBank::floating;
    return ValueBank::integer;
}

std::uint16_t spill_size(const machine::Function& function, machine::VirtualRegister reg) noexcept {
    if (value_bank(function, reg) != ValueBank::vector) return 8U;
    const auto width = reg < function.register_widths.size() ? function.register_widths[reg] : std::uint8_t{16U};
    if (width > 32U) return 64U;
    if (width > 16U) return 32U;
    return 16U;
}

std::uint16_t spill_alignment(std::uint16_t size) noexcept {
    return size >= 16U ? 16U : 8U;
}

std::uint32_t align_up(std::uint32_t value, std::uint32_t alignment) noexcept {
    return (value + alignment - 1U) & ~(alignment - 1U);
}

bool is_call(machine::Opcode opcode) noexcept {
    switch (opcode) {
    case machine::Opcode::call_i32: case machine::Opcode::call_i64:
    case machine::Opcode::call_f32: case machine::Opcode::call_f64:
    case machine::Opcode::call_void: case machine::Opcode::call_aggregate:
    case machine::Opcode::call_indirect_i32: case machine::Opcode::call_indirect_i64:
    case machine::Opcode::call_indirect_f32: case machine::Opcode::call_indirect_f64:
    case machine::Opcode::call_indirect_void: return true;
    default: return false;
    }
}

std::uint64_t spill_priority(const machine::LiveInterval& interval) noexcept {
    const auto length = static_cast<std::uint64_t>(interval.end - interval.start + 1U);
    return (static_cast<std::uint64_t>(interval.spill_weight) + 1U) * 1024U /
           std::max<std::uint64_t>(1U, length);
}

void remember_register(std::vector<std::uint8_t>& registers, std::uint8_t physical) {
    if (std::find(registers.begin(), registers.end(), physical) == registers.end())
        registers.push_back(physical);
}

} // namespace

RegisterAllocation allocate_registers(const machine::Function& function) {
    RegisterAllocation allocation;
    if (function.register_count > 65536U) {
        allocation.diagnostics.push_back({DiagnosticSeverity::error,
            "AArch64 linear-scan virtual-register limit exceeded in @" + function.name, {}});
        return allocation;
    }

    allocation.intervals = machine::compute_live_intervals(function);
    allocation.locations.resize(function.register_count);
    for (auto& location : allocation.locations) {
        location.spill_slot = std::numeric_limits<std::uint32_t>::max();
        location.spill_offset = std::numeric_limits<std::uint32_t>::max();
    }
    std::vector<bool> spilled_marked(function.register_count, false);
    std::vector<bool> dead_constant_candidate(function.register_count, false);
    std::vector<bool> vector_crosses_call(function.register_count, false);

    // v8-v15 preserve only their low 64 bits under AAPCS64. Full Q values that
    // are live after a call therefore cannot be assigned to a nominally
    // callee-saved SIMD register; mark them for a full-width stack home.
    const auto liveness = machine::analyze_liveness(function, true);
    for (std::size_t block_index = 0; block_index < function.blocks.size(); ++block_index) {
        const auto& block = function.blocks[block_index];
        for (std::size_t instruction_index = 0; instruction_index < block.instructions.size(); ++instruction_index) {
            if (!is_call(block.instructions[instruction_index].opcode)) continue;
            liveness.live_after[block_index][instruction_index].for_each_set_bit([&](machine::VirtualRegister reg) {
                if (value_bank(function, reg) == ValueBank::vector) vector_crosses_call[reg] = true;
            });
        }
    }

    std::vector<machine::VirtualRegister> copy_sources(function.register_count, function.register_count);
    for (const auto& block : function.blocks) {
        for (const auto& instruction : block.instructions) {
            if ((instruction.opcode == machine::Opcode::load_immediate ||
                 instruction.opcode == machine::Opcode::load_immediate_i64) &&
                instruction.result < function.register_count)
                dead_constant_candidate[instruction.result] = true;
            if ((instruction.opcode == machine::Opcode::copy || instruction.opcode == machine::Opcode::copy_f32 ||
                 instruction.opcode == machine::Opcode::copy_f64) && instruction.inputs.size() == 1U &&
                instruction.result < function.register_count)
                copy_sources[instruction.result] = instruction.inputs.front();
        }
    }

    std::vector<const machine::LiveInterval*> ordered;
    ordered.reserve(allocation.intervals.size());
    for (const auto& interval : allocation.intervals) ordered.push_back(&interval);
    std::stable_sort(ordered.begin(), ordered.end(), [](const auto* left, const auto* right) {
        if (left->start != right->start) return left->start < right->start;
        if (left->end != right->end) return left->end < right->end;
        return left->virtual_register < right->virtual_register;
    });

    std::vector<Active> integer_active;
    std::vector<Active> floating_active;
    std::vector<Active> vector_active;
    std::vector<std::uint8_t> free_integer(integer_pool.begin(), integer_pool.end());
    std::vector<std::uint8_t> free_floating(floating_pool.begin(), floating_pool.end());
    std::vector<std::uint8_t> free_vector(vector_pool.begin(), vector_pool.end());

    const auto spill = [&](machine::VirtualRegister reg) {
        auto& location = allocation.locations[reg];
        if (spilled_marked[reg]) return;
        location = {AllocationKind::stack_slot, 0U, std::numeric_limits<std::uint32_t>::max(),
                    std::numeric_limits<std::uint32_t>::max(), spill_size(function, reg)};
        spilled_marked[reg] = true;
        ++allocation.spilled_value_count;
        if (value_bank(function, reg) == ValueBank::vector) ++allocation.vector_spilled_value_count;
    };

    const auto assign_register = [&](machine::VirtualRegister reg, const machine::LiveInterval& interval,
                                     ValueBank bank, std::uint8_t physical, std::vector<Active>& active) {
        const auto kind = bank == ValueBank::integer ? AllocationKind::integer_register :
                          bank == ValueBank::floating ? AllocationKind::floating_register :
                                                        AllocationKind::vector_register;
        allocation.locations[reg] = {kind, physical, 0U, 0U, spill_size(function, reg)};
        active.push_back({interval, physical});
        ++allocation.physical_value_count;
        if (bank == ValueBank::vector) ++allocation.vector_register_value_count;
        else if (bank == ValueBank::floating) remember_register(allocation.used_floating_callee_saved, physical);
        else remember_register(allocation.used_integer_callee_saved, physical);
    };

    for (const auto* incoming_ptr : ordered) {
        const auto& incoming = *incoming_ptr;
        const auto reg = incoming.virtual_register;
        if (incoming.use_count == 0U && reg < dead_constant_candidate.size() && dead_constant_candidate[reg])
            continue;
        const auto bank = value_bank(function, reg);
        if (bank == ValueBank::vector && vector_crosses_call[reg]) {
            spill(reg);
            continue;
        }
        auto& active = bank == ValueBank::integer ? integer_active :
                       bank == ValueBank::floating ? floating_active : vector_active;
        auto& free = bank == ValueBank::integer ? free_integer :
                     bank == ValueBank::floating ? free_floating : free_vector;

        for (auto iterator = active.begin(); iterator != active.end();) {
            if (iterator->interval.end < incoming.start) {
                free.push_back(iterator->physical);
                iterator = active.erase(iterator);
            } else {
                ++iterator;
            }
        }

        // Scalar copy coalescing. The current machine IR has no vector-copy
        // opcode, so vector values are intentionally excluded here.
        const auto source = reg < copy_sources.size() ? copy_sources[reg] : function.register_count;
        if (bank != ValueBank::vector && source < function.register_count && source < allocation.intervals.size() &&
            value_bank(function, source) == bank && allocation.intervals[source].end == incoming.start) {
            const auto source_active = std::find_if(active.begin(), active.end(), [&](const Active& candidate) {
                return candidate.interval.virtual_register == source;
            });
            if (source_active != active.end()) {
                const auto physical = source_active->physical;
                active.erase(source_active);
                assign_register(reg, incoming, bank, physical, active);
                ++allocation.coalesced_copy_count;
                continue;
            }
        }

        if (!free.empty()) {
            const auto physical = free.back();
            free.pop_back();
            assign_register(reg, incoming, bank, physical, active);
            continue;
        }

        auto candidate = std::min_element(active.begin(), active.end(), [](const Active& left, const Active& right) {
            const auto left_priority = spill_priority(left.interval);
            const auto right_priority = spill_priority(right.interval);
            if (left_priority != right_priority) return left_priority < right_priority;
            return left.interval.end > right.interval.end;
        });
        const auto incoming_priority = spill_priority(incoming);
        const bool evict = candidate != active.end() &&
            (spill_priority(candidate->interval) < incoming_priority ||
             (spill_priority(candidate->interval) == incoming_priority && candidate->interval.end > incoming.end));
        if (!evict) {
            spill(reg);
            continue;
        }

        const auto physical = candidate->physical;
        const auto evicted = candidate->interval.virtual_register;
        const auto evicted_bank = value_bank(function, evicted);
        spill(evicted);
        if (allocation.physical_value_count != 0U) --allocation.physical_value_count;
        if (evicted_bank == ValueBank::vector && allocation.vector_register_value_count != 0U)
            --allocation.vector_register_value_count;
        // Remove a callee-saved register from the summary only when no remaining
        // allocation still uses it; the final reconciliation below handles it.
        const auto kind = bank == ValueBank::integer ? AllocationKind::integer_register :
                          bank == ValueBank::floating ? AllocationKind::floating_register :
                                                        AllocationKind::vector_register;
        allocation.locations[reg] = {kind, physical, 0U, 0U, spill_size(function, reg)};
        *candidate = {incoming, physical};
        ++allocation.physical_value_count;
        if (bank == ValueBank::vector) ++allocation.vector_register_value_count;
    }

    // Rebuild scalar callee-saved summaries after eviction so registers used
    // only by an evicted interval do not inflate the frame.
    allocation.used_integer_callee_saved.clear();
    allocation.used_floating_callee_saved.clear();
    for (machine::VirtualRegister reg = 0; reg < function.register_count; ++reg) {
        const auto& location = allocation.locations[reg];
        if (location.kind == AllocationKind::integer_register)
            remember_register(allocation.used_integer_callee_saved, location.physical);
        else if (location.kind == AllocationKind::floating_register)
            remember_register(allocation.used_floating_callee_saved, location.physical);
    }

    // Recover spills caused only by holes in bounding intervals. Vector values
    // live across calls remain pinned to memory because v16-v23 are caller-saved.
    const auto register_available = [&](machine::VirtualRegister reg, AllocationKind kind, std::uint8_t physical) {
        for (machine::VirtualRegister other = 0; other < function.register_count; ++other) {
            if (other == reg) continue;
            const auto& location = allocation.locations[other];
            if (location.kind != kind || location.physical != physical) continue;
            if (segments_overlap(allocation.intervals[reg], allocation.intervals[other])) return false;
        }
        return true;
    };
    for (machine::VirtualRegister reg = 0; reg < function.register_count; ++reg) {
        auto& location = allocation.locations[reg];
        if (location.kind != AllocationKind::stack_slot || allocation.intervals[reg].use_count == 0U) continue;
        const auto bank = value_bank(function, reg);
        if (bank == ValueBank::vector && vector_crosses_call[reg]) continue;
        const auto kind = bank == ValueBank::integer ? AllocationKind::integer_register :
                          bank == ValueBank::floating ? AllocationKind::floating_register :
                                                        AllocationKind::vector_register;
        const auto try_pool = [&](const auto& pool) {
            for (const auto physical : pool) {
                if (!register_available(reg, kind, physical)) continue;
                location = {kind, physical, 0U, 0U, spill_size(function, reg)};
                spilled_marked[reg] = false;
                --allocation.spilled_value_count;
                ++allocation.physical_value_count;
                ++allocation.hole_aware_register_reuse_count;
                if (bank == ValueBank::vector) {
                    --allocation.vector_spilled_value_count;
                    ++allocation.vector_register_value_count;
                } else if (bank == ValueBank::floating) {
                    remember_register(allocation.used_floating_callee_saved, physical);
                } else {
                    remember_register(allocation.used_integer_callee_saved, physical);
                }
                return true;
            }
            return false;
        };
        if (bank == ValueBank::integer) (void)try_pool(integer_pool);
        else if (bank == ValueBank::floating) (void)try_pool(floating_pool);
        else (void)try_pool(vector_pool);
    }

    struct SpillColor {
        std::uint16_t size{};
        std::uint32_t offset{};
        std::vector<machine::VirtualRegister> owners;
    };
    std::vector<SpillColor> colors;
    std::vector<machine::VirtualRegister> spilled;
    spilled.reserve(allocation.spilled_value_count);
    std::uint32_t naive_spill_bytes = 0U;
    for (machine::VirtualRegister reg = 0; reg < function.register_count; ++reg) {
        if (!spilled_marked[reg]) continue;
        spilled.push_back(reg);
        naive_spill_bytes += spill_size(function, reg);
    }
    std::stable_sort(spilled.begin(), spilled.end(), [&](auto left, auto right) {
        const auto& lhs = allocation.intervals[left];
        const auto& rhs = allocation.intervals[right];
        if (lhs.start != rhs.start) return lhs.start < rhs.start;
        if (spill_size(function, left) != spill_size(function, right))
            return spill_size(function, left) > spill_size(function, right);
        return left < right;
    });

    for (const auto reg : spilled) {
        const auto required = spill_size(function, reg);
        std::uint32_t selected = std::numeric_limits<std::uint32_t>::max();
        std::uint16_t selected_size = std::numeric_limits<std::uint16_t>::max();
        for (std::uint32_t slot = 0; slot < colors.size(); ++slot) {
            if (colors[slot].size < required) continue;
            const bool conflict = std::any_of(colors[slot].owners.begin(), colors[slot].owners.end(), [&](auto owner) {
                return segments_overlap(allocation.intervals[reg], allocation.intervals[owner]);
            });
            if (conflict || colors[slot].size >= selected_size) continue;
            selected = slot;
            selected_size = colors[slot].size;
        }
        if (selected == std::numeric_limits<std::uint32_t>::max()) {
            selected = static_cast<std::uint32_t>(colors.size());
            colors.push_back({required, 0U, {}});
        }
        allocation.locations[reg].spill_slot = selected;
        allocation.locations[reg].spill_size = required;
        colors[selected].owners.push_back(reg);
    }

    std::uint32_t cursor = 0U;
    for (auto& color : colors) {
        cursor = align_up(cursor, spill_alignment(color.size));
        color.offset = cursor;
        cursor += color.size;
        for (const auto owner : color.owners)
            allocation.locations[owner].spill_offset = color.offset;
    }
    allocation.spill_slot_count = static_cast<std::uint32_t>(colors.size());
    allocation.reused_spill_slot_count = allocation.spilled_value_count > allocation.spill_slot_count
        ? allocation.spilled_value_count - allocation.spill_slot_count : 0U;
    allocation.spill_bytes = align_up(cursor, 16U);
    allocation.frame_bytes_saved = naive_spill_bytes > allocation.spill_bytes
        ? naive_spill_bytes - allocation.spill_bytes : 0U;

    std::sort(allocation.used_integer_callee_saved.begin(), allocation.used_integer_callee_saved.end());
    std::sort(allocation.used_floating_callee_saved.begin(), allocation.used_floating_callee_saved.end());
    return allocation;
}

} // namespace forge::codegen::aarch64
