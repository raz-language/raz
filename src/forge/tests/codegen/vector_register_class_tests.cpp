// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

// Packed values share the XMM/YMM/ZMM physical file with scalar floats, so the
// allocator must treat them as one pool. A separate pool over the same
// registers would let a float and a packed value be handed the same register.

#include <cstdlib>
#include <iostream>
#include <string>

#include "forge/machine/module.hpp"
#include "forge/machine/register_allocation.hpp"

namespace {

int failures = 0;

void check(bool condition, const std::string& what) {
    if (condition) return;
    std::cerr << "FAIL " << what << '\n';
    ++failures;
}

using forge::machine::Block;
using forge::machine::Function;
using forge::machine::Instruction;
using forge::machine::LocationKind;
using forge::machine::Opcode;
using forge::machine::RegisterClass;
using forge::machine::VirtualRegister;

// A function whose values are all packed, defined and then all consumed at the
// end so their live ranges overlap and the pool is genuinely contested.
Function make_vector_function(std::uint32_t value_count, std::uint8_t width) {
    Function function;
    function.name = "packed";
    function.register_count = value_count + 1U;
    for (std::uint32_t index = 0; index < value_count; ++index) {
        function.register_widths.push_back(width);
        function.register_classes.push_back(RegisterClass::vector);
    }
    function.register_widths.push_back(8U);
    function.register_classes.push_back(RegisterClass::integer);

    Block entry;
    entry.name = "entry";
    for (std::uint32_t index = 0; index < value_count; ++index) {
        Instruction define;
        define.opcode = Opcode::load_stack_v128;
        define.result = index;
        define.immediate = -16 * static_cast<std::int64_t>(index + 1U);
        define.vector_bits = static_cast<std::uint16_t>(width) * 8U;
        entry.instructions.push_back(std::move(define));
    }
    for (std::uint32_t index = 0; index < value_count; ++index) {
        Instruction use;
        use.opcode = Opcode::store_stack_v128;
        use.inputs.push_back(index);
        use.immediate = -16 * static_cast<std::int64_t>(index + 1U);
        entry.instructions.push_back(std::move(use));
    }
    Instruction ret;
    ret.opcode = Opcode::return_void;
    entry.instructions.push_back(std::move(ret));
    function.blocks.push_back(std::move(entry));
    return function;
}

void test_packed_values_use_the_vector_file() {
    const auto function = make_vector_function(2, 16);
    const auto allocation = forge::machine::allocate_linear_scan(function);
    check(allocation.ok(), "allocating packed values succeeds");
    if (!allocation.ok()) return;

    for (VirtualRegister reg = 0; reg < 2U; ++reg) {
        const auto& location = allocation.location(reg);
        const bool placed = location.kind == LocationKind::floating_register ||
                            location.kind == LocationKind::stack_slot;
        check(placed, "a packed value lands in the vector file or on the stack");
        check(location.kind != LocationKind::physical_register,
              "a packed value never lands in an integer register");
    }
}

void test_packed_and_float_do_not_share_a_register() {
    // Two packed values and two floats, all live at once. If the classes were
    // allocated from separate pools over the same physical registers, two of
    // these would be given the same XMM register.
    Function function;
    function.name = "mixed";
    function.register_count = 4U;
    function.register_widths = {16U, 16U, 8U, 8U};
    function.register_classes = {RegisterClass::vector, RegisterClass::vector,
                                 RegisterClass::floating, RegisterClass::floating};

    Block entry;
    entry.name = "entry";
    for (VirtualRegister reg = 0; reg < 4U; ++reg) {
        Instruction define;
        define.opcode = reg < 2U ? Opcode::load_stack_v128 : Opcode::load_stack_f64;
        define.result = reg;
        define.immediate = -16 * static_cast<std::int64_t>(reg + 1U);
        entry.instructions.push_back(std::move(define));
    }
    for (VirtualRegister reg = 0; reg < 4U; ++reg) {
        Instruction use;
        use.opcode = reg < 2U ? Opcode::store_stack_v128 : Opcode::store_stack_f64;
        use.inputs.push_back(reg);
        use.immediate = -16 * static_cast<std::int64_t>(reg + 1U);
        entry.instructions.push_back(std::move(use));
    }
    Instruction ret;
    ret.opcode = Opcode::return_void;
    entry.instructions.push_back(std::move(ret));
    function.blocks.push_back(std::move(entry));

    const auto allocation = forge::machine::allocate_linear_scan(function);
    check(allocation.ok(), "allocating mixed float/packed values succeeds");
    if (!allocation.ok()) return;

    for (VirtualRegister left = 0; left < 4U; ++left) {
        for (VirtualRegister right = left + 1U; right < 4U; ++right) {
            const auto& a = allocation.location(left);
            const auto& b = allocation.location(right);
            if (a.kind != LocationKind::floating_register) continue;
            if (b.kind != LocationKind::floating_register) continue;
            check(a.floating != b.floating,
                  "two simultaneously live values never share one XMM register");
        }
    }
}

void test_spill_slots_are_wide_enough() {
    // More packed values than the pool holds forces spilling. Each spilled
    // 16-byte value needs a 16-byte slot; the frame must reflect that rather
    // than the eight bytes a scalar would take.
    const auto function = make_vector_function(12, 16);
    const auto allocation = forge::machine::allocate_linear_scan(function);
    check(allocation.ok(), "allocating past the vector pool succeeds");
    if (!allocation.ok()) return;

    std::uint32_t spilled = 0;
    for (VirtualRegister reg = 0; reg < 12U; ++reg) {
        if (allocation.location(reg).kind == LocationKind::stack_slot) ++spilled;
    }
    check(spilled > 0, "contested packed values spill");
    if (spilled == 0) return;
    check(allocation.frame_size >= spilled * 16U,
          "the frame reserves at least sixteen bytes per spilled packed value, got " +
              std::to_string(allocation.frame_size) + " for " + std::to_string(spilled));
}

} // namespace

int main() {
    test_packed_values_use_the_vector_file();
    test_packed_and_float_do_not_share_a_register();
    test_spill_slots_are_wide_enough();
    if (failures != 0) {
        std::cerr << failures << " vector register class check(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "vector register class: shared XMM pool, no aliasing, wide spill slots PASS\n";
    return EXIT_SUCCESS;
}
