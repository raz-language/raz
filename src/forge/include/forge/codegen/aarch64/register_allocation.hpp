// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <vector>

#include "forge/diagnostics/diagnostic.hpp"
#include "forge/machine/liveness.hpp"
#include "forge/machine/module.hpp"
#include "forge/machine/register_allocation.hpp"

namespace forge::codegen::aarch64 {

enum class AllocationKind : std::uint8_t {
    integer_register,
    floating_register,
    vector_register,
    stack_slot,
};

struct AllocationLocation {
    AllocationKind kind{AllocationKind::stack_slot};
    // Architectural register number for register locations (xN or vN).
    std::uint8_t physical{};
    // Stable spill color used by diagnostics/tests. `spill_offset` is the byte
    // offset inside the allocation spill area and is what the encoder uses.
    std::uint32_t spill_slot{};
    std::uint32_t spill_offset{};
    std::uint16_t spill_size{8U};
};

struct RegisterAllocation {
    std::vector<AllocationLocation> locations;
    std::vector<machine::LiveInterval> intervals;
    std::vector<std::uint8_t> used_integer_callee_saved;
    std::vector<std::uint8_t> used_floating_callee_saved;
    std::uint32_t physical_value_count{};
    std::uint32_t vector_register_value_count{};
    std::uint32_t spilled_value_count{};
    std::uint32_t vector_spilled_value_count{};
    std::uint32_t spill_slot_count{};
    // Size of each colored spill home. Keeping this explicit makes wide-vector
    // allocation observable to diagnostics/tests and avoids inferring a slot's
    // width from whichever virtual value happens to own it first.
    std::vector<std::uint16_t> spill_slot_sizes;
    std::uint32_t spill_bytes{};
    std::uint32_t reused_spill_slot_count{};
    std::uint32_t frame_bytes_saved{};
    std::uint32_t coalesced_copy_count{};
    std::uint32_t hole_aware_register_reuse_count{};
    Diagnostics diagnostics;

    [[nodiscard]] bool ok() const noexcept { return diagnostics.empty(); }
    [[nodiscard]] const AllocationLocation& location(machine::VirtualRegister reg) const {
        return locations.at(reg);
    }
};

// A correctness-first AAPCS64 allocator. Integer and scalar FP values use the
// ABI callee-saved banks (x19-x28 and the low 64 bits of v8-v15). Full 128-bit
// vectors cannot rely on v8-v15 across calls because AAPCS64 preserves only the
// low 64 bits of those registers, so call-local vector values use caller-saved
// v16-v23 and vectors live across a call are spilled to 16-byte homes.
[[nodiscard]] RegisterAllocation allocate_registers(const machine::Function& function);

} // namespace forge::codegen::aarch64
