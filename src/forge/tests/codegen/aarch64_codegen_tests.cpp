// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <unordered_set>

#include "forge/codegen/aarch64/encoder.hpp"
#include "forge/codegen/aarch64/register_allocation.hpp"
#include "forge/ir/parser.hpp"
#include "forge/ir/verifier.hpp"
#include "forge/machine/lower.hpp"
#include "forge/machine/verifier.hpp"

namespace {
int failures = 0;
void check(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAIL " << message << '\n';
    ++failures;
}
}

int main() {
    const std::string source = R"(
module @a64_codegen {
  global @counter: i64 = 7
  thread_local global @tls_counter: i64 = 9
  extern func @host_sum10(%a0: i64, %a1: i64, %a2: i64, %a3: i64, %a4: i64, %a5: i64, %a6: i64, %a7: i64, %a8: i64, %a9: i64) -> i64
  func @entry(%value: i64) -> i64 {
  entry:
    %g = global.address ptr @counter
    %gv = load i64 %g align 8
    %t = tls.address ptr @tls_counter
    %tv = load i64 %t align 8
    %r = call i64 @host_sum10(%value, %gv, %tv, %value, %gv, %tv, %value, %gv, %tv, %value)
    return %r
  }
}
)";

    auto parsed = forge::ir::parse_module(source);
    check(parsed.ok(), "AArch64 stress IR parses");
    if (!parsed.ok()) return EXIT_FAILURE;
    check(forge::ir::verify_module(*parsed.module).empty(), "AArch64 stress IR verifies");

    auto lowered = forge::machine::lower_module(
        *parsed.module, {forge::machine::TargetArchitecture::aarch64});
    check(lowered.ok(), "AArch64 stress IR lowers");
    if (!lowered.ok()) return EXIT_FAILURE;

    auto encoded = forge::codegen::aarch64::encode(*lowered.module);
    check(encoded.ok(), "AArch64 stress module encodes");
    if (!encoded.ok()) return EXIT_FAILURE;
    check(encoded.functions.size() == 1U, "one AArch64 function emitted");
    if (!encoded.functions.empty()) {
        const auto& function = encoded.functions.front();
        check(!function.code.empty() && (function.code.size() % 4U) == 0U,
              "AArch64 instructions are fixed-width words");
        check(function.abi_stack_argument_count >= 2U,
              "AAPCS64 call spills arguments beyond x0-x7 to the stack");
        check(function.register_allocated_value_count != 0U,
              "AArch64 linear scan assigns virtual values to physical registers");
        check(function.callee_saved_register_count != 0U,
              "AArch64 encoder preserves the physical registers used by allocation");
        check(function.spilled_value_count < lowered.module->functions.front().register_count,
              "AArch64 allocation avoids stack-homing every virtual value");
    }

    auto image = forge::codegen::aarch64::encode_image(*lowered.module);
    check(image.ok(), "AArch64 module image encodes");
    if (image.ok()) {
        using R = forge::codegen::aarch64::RelocationKind;
        const auto has = [&](R kind) {
            return std::any_of(image.image.relocations.begin(), image.image.relocations.end(),
                               [&](const auto& relocation) { return relocation.kind == kind; });
        };
        check(has(R::call26), "external direct calls use CALL26 relocations");
        check(has(R::adr_prel_pg_hi21) && has(R::add_abs_lo12_nc),
              "global addresses use ADRP+ADD relocations");
        check(has(R::tlsie_adr_gottprel_page21) && has(R::tlsie_ld64_gottprel_lo12_nc),
              "TLS uses Linux initial-exec GOTTPREL relocations");
    }


    const std::string pressure_source = R"(
module @a64_pressure {
  extern func @sink14(%a0: i64, %a1: i64, %a2: i64, %a3: i64, %a4: i64, %a5: i64, %a6: i64, %a7: i64, %a8: i64, %a9: i64, %a10: i64, %a11: i64, %a12: i64, %a13: i64) -> i64
  func @pressure() -> i64 {
  entry:
    %v0 = const i64 100
    %v1 = const i64 101
    %v2 = const i64 102
    %v3 = const i64 103
    %v4 = const i64 104
    %v5 = const i64 105
    %v6 = const i64 106
    %v7 = const i64 107
    %v8 = const i64 108
    %v9 = const i64 109
    %v10 = const i64 110
    %v11 = const i64 111
    %v12 = const i64 112
    %v13 = const i64 113
    %result = call i64 @sink14(%v0, %v1, %v2, %v3, %v4, %v5, %v6, %v7, %v8, %v9, %v10, %v11, %v12, %v13)
    return %result
  }
}
)";
    auto pressure_parsed = forge::ir::parse_module(pressure_source);
    check(pressure_parsed.ok(), "AArch64 pressure IR parses");
    if (pressure_parsed.ok()) {
        auto pressure_lowered = forge::machine::lower_module(
            *pressure_parsed.module, {forge::machine::TargetArchitecture::aarch64});
        check(pressure_lowered.ok(), "AArch64 pressure IR lowers");
        if (pressure_lowered.ok()) {
            const auto& function = pressure_lowered.module->functions.front();
            auto allocation = forge::codegen::aarch64::allocate_registers(function);
            check(allocation.ok(), "AArch64 physical register allocation succeeds under pressure");
            check(allocation.physical_value_count >= 10U,
                  "AArch64 allocator fills the integer callee-saved bank");
            check(allocation.spilled_value_count >= 4U,
                  "AArch64 allocator spills when more than ten integer values overlap");
            std::unordered_set<std::uint32_t> spill_slots;
            for (const auto& location : allocation.locations) {
                if (location.kind == forge::codegen::aarch64::AllocationKind::stack_slot)
                    spill_slots.insert(location.spill_slot);
                if (location.kind == forge::codegen::aarch64::AllocationKind::integer_register)
                    check(location.physical >= 19U && location.physical <= 28U,
                          "AArch64 integer allocation stays inside x19-x28");
            }
            check(spill_slots.size() == allocation.spilled_value_count,
                  "AArch64 spills receive distinct stack slots");
            auto pressure_encoded = forge::codegen::aarch64::encode(*pressure_lowered.module);
            check(pressure_encoded.ok(), "AArch64 pressure function encodes with physical allocation");
            if (pressure_encoded.ok() && !pressure_encoded.functions.empty()) {
                check(pressure_encoded.functions.front().abi_stack_argument_count >= 6U,
                      "AAPCS64 pressure call places arguments beyond x0-x7 on the stack");
            }
        }
    }

    const std::string immediate_source = R"(
module @a64_immediates {
  func @immediates(%value: i64) -> i64 {
  entry:
    %three = const i64 3
    %added = add i64 %value %three
    %two = const i64 2
    %shifted = shl i64 %added %two
    %limit = const i64 4096
    %less = cmp.lt i64 %shifted %limit
    %seven = const i64 7
    %subtracted = sub i64 %shifted %seven
    %selected = select i64 %less %subtracted %shifted
    return %selected
  }
}
)";
    auto immediate_parsed = forge::ir::parse_module(immediate_source);
    check(immediate_parsed.ok(), "AArch64 immediate-selection IR parses");
    if (immediate_parsed.ok()) {
        auto immediate_lowered = forge::machine::lower_module(
            *immediate_parsed.module, {forge::machine::TargetArchitecture::aarch64});
        check(immediate_lowered.ok(), "AArch64 immediate-selection IR lowers");
        if (immediate_lowered.ok()) {
            bool saw_immediate = false;
            bool saw_compare_immediate = false;
            for (const auto& block : immediate_lowered.module->functions.front().blocks) {
                for (const auto& instruction : block.instructions) {
                    saw_immediate = saw_immediate || instruction.symbol == "$imm";
                    saw_compare_immediate = saw_compare_immediate || instruction.symbol == "$cmpimm";
                }
            }
            check(saw_immediate, "AArch64 lowering selects arithmetic/shift immediate pseudos");
            check(saw_compare_immediate, "AArch64 lowering selects compare immediate pseudos");
            auto immediate_encoded = forge::codegen::aarch64::encode(*immediate_lowered.module);
            check(immediate_encoded.ok(), "AArch64 immediate-selected function encodes");
            if (immediate_encoded.ok() && !immediate_encoded.functions.empty()) {
                const auto& stats = immediate_encoded.functions.front();
                check(stats.immediate_form_count >= 4U,
                      "AArch64 encoder emits native add/sub/shift/cmp immediate forms");
                check(stats.elided_dead_constant_count >= 4U,
                      "folded AArch64 constants are not materialized into registers");
            }
        }
    }

    const std::string reuse_source = R"(
module @a64_spill_reuse {
  extern func @sink14(%a0: i64, %a1: i64, %a2: i64, %a3: i64, %a4: i64, %a5: i64, %a6: i64, %a7: i64, %a8: i64, %a9: i64, %a10: i64, %a11: i64, %a12: i64, %a13: i64) -> i64
  func @reuse() -> i64 {
  entry:
    %a0 = const i64 1
    %a1 = const i64 2
    %a2 = const i64 3
    %a3 = const i64 4
    %a4 = const i64 5
    %a5 = const i64 6
    %a6 = const i64 7
    %a7 = const i64 8
    %a8 = const i64 9
    %a9 = const i64 10
    %a10 = const i64 11
    %a11 = const i64 12
    %a12 = const i64 13
    %a13 = const i64 14
    %first = call i64 @sink14(%a0, %a1, %a2, %a3, %a4, %a5, %a6, %a7, %a8, %a9, %a10, %a11, %a12, %a13)
    %b0 = const i64 21
    %b1 = const i64 22
    %b2 = const i64 23
    %b3 = const i64 24
    %b4 = const i64 25
    %b5 = const i64 26
    %b6 = const i64 27
    %b7 = const i64 28
    %b8 = const i64 29
    %b9 = const i64 30
    %b10 = const i64 31
    %b11 = const i64 32
    %b12 = const i64 33
    %b13 = const i64 34
    %second = call i64 @sink14(%b0, %b1, %b2, %b3, %b4, %b5, %b6, %b7, %b8, %b9, %b10, %b11, %b12, %b13)
    %result = add i64 %first %second
    return %result
  }
}
)";
    auto reuse_parsed = forge::ir::parse_module(reuse_source);
    check(reuse_parsed.ok(), "AArch64 spill-slot-reuse IR parses");
    if (reuse_parsed.ok()) {
        auto reuse_lowered = forge::machine::lower_module(
            *reuse_parsed.module, {forge::machine::TargetArchitecture::aarch64});
        check(reuse_lowered.ok(), "AArch64 spill-slot-reuse IR lowers");
        if (reuse_lowered.ok()) {
            auto reuse_allocation = forge::codegen::aarch64::allocate_registers(reuse_lowered.module->functions.front());
            check(reuse_allocation.ok(), "AArch64 spill-slot coloring succeeds");
            check(reuse_allocation.reused_spill_slot_count != 0U,
                  "disjoint AArch64 spills reuse stack slots");
            check(reuse_allocation.spill_slot_count < reuse_allocation.spilled_value_count,
                  "AArch64 spill-slot coloring shrinks the frame home area");
            check(reuse_allocation.frame_bytes_saved >= 8U,
                  "AArch64 spill-slot reuse reports concrete frame-byte savings");
        }
    }



    // Direct packed-machine regression: the first NEON slice lowers the shared
    // memory-to-memory vector pseudos without adding target-specific public IR.
    // Four i32 lanes are exactly one Q register, so this fixture must emit a
    // single ADD v0.4s and report one native vector ALU operation.
    {
        forge::machine::Module neon_module;
        neon_module.name = "a64_neon";
        forge::machine::Function neon_function;
        neon_function.name = "map_add4";
        neon_function.argument_count = 3U;
        neon_function.argument_widths = {8U, 8U, 8U};
        neon_function.argument_classes = {forge::machine::RegisterClass::integer,
                                           forge::machine::RegisterClass::integer,
                                           forge::machine::RegisterClass::integer};
        neon_function.register_count = 3U;
        neon_function.register_widths = {8U, 8U, 8U};
        neon_function.register_classes = neon_function.argument_classes;
        forge::machine::Block entry;
        entry.name = "entry";
        for (std::uint32_t index = 0; index < 3U; ++index) {
            forge::machine::Instruction argument;
            argument.opcode = forge::machine::Opcode::load_argument_i64;
            argument.result = index;
            argument.argument_index = index;
            entry.instructions.push_back(argument);
        }
        forge::machine::Instruction packed;
        packed.opcode = forge::machine::Opcode::binary_i32_contiguous_map;
        packed.inputs = {0U, 1U, 2U};
        packed.immediate = 4;
        packed.argument_index = static_cast<std::uint32_t>(forge::machine::Opcode::add_i32);
        packed.vector_bits = 128U;
        entry.instructions.push_back(packed);
        forge::machine::Instruction done;
        done.opcode = forge::machine::Opcode::return_void;
        entry.instructions.push_back(done);
        neon_function.blocks.push_back(std::move(entry));
        neon_module.functions.push_back(std::move(neon_function));

        const auto verified = forge::machine::verify_module(neon_module);
        check(verified.empty(), "AArch64 NEON packed machine fixture verifies");
        auto neon_encoded = forge::codegen::aarch64::encode(neon_module);
        check(neon_encoded.ok(), "AArch64 NEON packed machine fixture encodes");
        if (neon_encoded.ok() && !neon_encoded.functions.empty()) {
            const auto& encoded_function = neon_encoded.functions.front();
            check(encoded_function.neon_vector_operation_count == 1U,
                  "AArch64 NEON pack reports one native vector ALU operation");
            bool saw_add_4s = false;
            for (std::size_t offset = 0; offset + 4U <= encoded_function.code.size(); offset += 4U) {
                const auto word = static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(encoded_function.code[offset])) |
                    (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(encoded_function.code[offset + 1U])) << 8U) |
                    (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(encoded_function.code[offset + 2U])) << 16U) |
                    (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(encoded_function.code[offset + 3U])) << 24U);
                saw_add_4s = saw_add_4s || word == 0x4EA18400U; // add v0.4s, v0.4s, v1.4s
            }
            check(saw_add_4s, "AArch64 NEON pack emits ADD v0.4s, v0.4s, v1.4s");
        }
    }



    // Normal FIR should reach NEON automatically when aliasing is provably
    // in-place. Four adjacent i32 lanes collapse to one shared packed pseudo,
    // and the encoder then emits one 128-bit Advanced SIMD operation.
    {
        const std::string auto_neon_source = R"(
module @a64_auto_neon {
  func @map_inplace4(%base: ptr, %delta: i32) -> void {
  entry:
    %p0 = ptr.offset ptr %base 0
    %v0 = load i32 %p0
    %r0 = add i32 %v0 %delta
    store i32 %r0 %p0
    %p1 = ptr.offset ptr %base 4
    %v1 = load i32 %p1
    %r1 = add i32 %v1 %delta
    store i32 %r1 %p1
    %p2 = ptr.offset ptr %base 8
    %v2 = load i32 %p2
    %r2 = add i32 %v2 %delta
    store i32 %r2 %p2
    %p3 = ptr.offset ptr %base 12
    %v3 = load i32 %p3
    %r3 = add i32 %v3 %delta
    store i32 %r3 %p3
    return
  }
}
)";
        auto auto_neon_parsed = forge::ir::parse_module(auto_neon_source);
        check(auto_neon_parsed.ok(), "AArch64 automatic NEON FIR parses");
        if (auto_neon_parsed.ok()) {
            auto auto_neon_lowered = forge::machine::lower_module(
                *auto_neon_parsed.module, {forge::machine::TargetArchitecture::aarch64});
            check(auto_neon_lowered.ok(), "AArch64 automatic NEON FIR lowers");
            if (auto_neon_lowered.ok()) {
                bool saw_pack = false;
                for (const auto& block : auto_neon_lowered.module->functions.front().blocks)
                    for (const auto& instruction : block.instructions)
                        if (instruction.opcode == forge::machine::Opcode::binary_i32_contiguous_inplace) {
                            saw_pack = true;
                            check(instruction.inputs.size() == 2U && instruction.inputs[0] != instruction.inputs[1],
                                  "AArch64 vreg compaction preserves distinct map base/scalar operands");
                        }
                check(saw_pack, "AArch64 canonical optimizer forms a 128-bit in-place NEON pack");
                check(auto_neon_lowered.module->functions.front().register_count <= 3U,
                      "AArch64 NEON SLP compacts eliminated scalar virtual registers");
                auto auto_neon_encoded = forge::codegen::aarch64::encode(*auto_neon_lowered.module);
                check(auto_neon_encoded.ok(), "AArch64 automatically packed NEON function encodes");
                if (auto_neon_encoded.ok() && !auto_neon_encoded.functions.empty())
                    check(auto_neon_encoded.functions.front().neon_vector_operation_count == 1U,
                          "automatic AArch64 NEON map emits one vector ALU operation");
            }
        }
    }


    // Full-width vector allocation/spilling. Q values use v16-v23 while they
    // are call-local; pressure spills are 16-byte homes, and a Q value live
    // across a call is always spilled because AAPCS64 preserves only the low
    // 64 bits of v8-v15.
    {
        forge::machine::Module vector_module;
        vector_module.name = "a64_vector_spills";
        forge::machine::Function vector_function;
        vector_function.name = "vector_pressure";
        vector_function.local_stack_size = 160U;
        vector_function.register_count = 10U;
        vector_function.register_widths.assign(10U, 16U);
        vector_function.register_classes.assign(10U, forge::machine::RegisterClass::vector);
        forge::machine::Block entry;
        entry.name = "entry";
        for (std::uint32_t reg = 0; reg < 10U; ++reg) {
            forge::machine::Instruction load;
            load.opcode = forge::machine::Opcode::load_stack_v128;
            load.result = reg;
            load.immediate = -16 * static_cast<std::int64_t>(reg + 1U);
            load.vector_bits = 128U;
            entry.instructions.push_back(std::move(load));
        }
        for (std::uint32_t reg = 0; reg < 10U; ++reg) {
            forge::machine::Instruction store;
            store.opcode = forge::machine::Opcode::store_stack_v128;
            store.inputs = {reg};
            store.immediate = -16 * static_cast<std::int64_t>(reg + 1U);
            store.vector_bits = 128U;
            entry.instructions.push_back(std::move(store));
        }
        forge::machine::Instruction ret;
        ret.opcode = forge::machine::Opcode::return_void;
        entry.instructions.push_back(std::move(ret));
        vector_function.blocks.push_back(std::move(entry));
        vector_module.functions.push_back(vector_function);

        const auto verified = forge::machine::verify_module(vector_module);
        check(verified.empty(), "AArch64 128-bit vector spill fixture verifies");
        const auto allocation = forge::codegen::aarch64::allocate_registers(vector_module.functions.front());
        check(allocation.ok(), "AArch64 128-bit vector allocation succeeds");
        check(allocation.vector_register_value_count == 8U,
              "AArch64 vector allocator fills v16-v23 under Q-register pressure");
        check(allocation.vector_spilled_value_count == 2U,
              "AArch64 vector allocator spills excess Q values");
        check(allocation.spill_bytes >= 32U,
              "AArch64 Q spills reserve full 16-byte homes");
        for (std::uint32_t reg = 0; reg < 10U; ++reg) {
            const auto& location = allocation.location(reg);
            if (location.kind == forge::codegen::aarch64::AllocationKind::vector_register)
                check(location.physical >= 16U && location.physical <= 23U,
                      "AArch64 vector allocation stays inside v16-v23");
            if (location.kind == forge::codegen::aarch64::AllocationKind::stack_slot)
                check(location.spill_size == 16U,
                      "AArch64 spilled Q value has a 16-byte stack home");
        }
        auto vector_encoded = forge::codegen::aarch64::encode(vector_module);
        check(vector_encoded.ok(), "AArch64 128-bit vector spill fixture encodes");

        auto call_function = vector_function;
        call_function.name = "vector_across_call";
        call_function.register_count = 1U;
        call_function.register_widths.assign(1U, 16U);
        call_function.register_classes.assign(1U, forge::machine::RegisterClass::vector);
        call_function.blocks.clear();
        forge::machine::Block call_entry;
        call_entry.name = "entry";
        forge::machine::Instruction load;
        load.opcode = forge::machine::Opcode::load_stack_v128;
        load.result = 0U;
        load.immediate = -16;
        load.vector_bits = 128U;
        call_entry.instructions.push_back(load);
        forge::machine::Instruction call;
        call.opcode = forge::machine::Opcode::call_void;
        call.symbol = "clobber";
        call_entry.instructions.push_back(call);
        forge::machine::Instruction store;
        store.opcode = forge::machine::Opcode::store_stack_v128;
        store.inputs = {0U};
        store.immediate = -16;
        store.vector_bits = 128U;
        call_entry.instructions.push_back(store);
        call_entry.instructions.push_back(ret);
        call_function.blocks.push_back(std::move(call_entry));
        const auto call_allocation = forge::codegen::aarch64::allocate_registers(call_function);
        check(call_allocation.ok(), "AArch64 live-across-call vector allocation succeeds");
        check(call_allocation.vector_spilled_value_count == 1U &&
              call_allocation.location(0U).kind == forge::codegen::aarch64::AllocationKind::stack_slot,
              "AAPCS64 full Q value live across a call is stack-homed");
        forge::machine::Module call_module;
        call_module.name = "a64_vector_call";
        call_module.functions.push_back(std::move(call_function));
        check(forge::codegen::aarch64::encode(call_module).ok(),
              "AArch64 live-across-call Q spill/reload encodes");
    }

    // Ordinary FIR reductions now reach NEON automatically on AArch64 rather
    // than remaining a scalar add tree.
    {
        const std::string reduction_source = R"(
module @a64_reductions {
  func @sum4_i32(%base: ptr) -> i32 {
  entry:
    %p0 = ptr.offset ptr %base 0
    %v0 = load i32 %p0
    %p1 = ptr.offset ptr %base 4
    %v1 = load i32 %p1
    %p2 = ptr.offset ptr %base 8
    %v2 = load i32 %p2
    %p3 = ptr.offset ptr %base 12
    %v3 = load i32 %p3
    %a0 = add i32 %v0 %v1
    %a1 = add i32 %v2 %v3
    %sum = add i32 %a0 %a1
    return %sum
  }
  func @sum4_i64(%base: ptr) -> i64 {
  entry:
    %p0 = ptr.offset ptr %base 0
    %v0 = load i64 %p0
    %p1 = ptr.offset ptr %base 8
    %v1 = load i64 %p1
    %p2 = ptr.offset ptr %base 16
    %v2 = load i64 %p2
    %p3 = ptr.offset ptr %base 24
    %v3 = load i64 %p3
    %a0 = add i64 %v0 %v1
    %a1 = add i64 %v2 %v3
    %sum = add i64 %a0 %a1
    return %sum
  }
}
)";
        auto parsed = forge::ir::parse_module(reduction_source);
        check(parsed.ok(), "AArch64 automatic reduction FIR parses");
        if (parsed.ok()) {
            auto lowered = forge::machine::lower_module(
                *parsed.module, {forge::machine::TargetArchitecture::aarch64});
            check(lowered.ok(), "AArch64 automatic reduction FIR lowers");
            if (lowered.ok()) {
                bool saw_i32 = false;
                bool saw_i64 = false;
                for (const auto& function : lowered.module->functions)
                    for (const auto& block : function.blocks) {
                        forge::machine::VirtualRegister reduction_result = function.register_count;
                        for (const auto& instruction : block.instructions) {
                            if (instruction.opcode == forge::machine::Opcode::reduce_add_i32_contiguous ||
                                instruction.opcode == forge::machine::Opcode::reduce_add_i64_contiguous)
                                reduction_result = instruction.result;
                            saw_i32 = saw_i32 || instruction.opcode == forge::machine::Opcode::reduce_add_i32_contiguous;
                            saw_i64 = saw_i64 || instruction.opcode == forge::machine::Opcode::reduce_add_i64_contiguous;
                            if ((instruction.opcode == forge::machine::Opcode::return_i32 ||
                                 instruction.opcode == forge::machine::Opcode::return_i64) &&
                                reduction_result < function.register_count)
                                check(instruction.inputs.size() == 1U && instruction.inputs.front() == reduction_result,
                                      "AArch64 vreg compaction preserves the reduction result consumed by return");
                        }
                    }
                check(saw_i32, "AArch64 optimizer forms contiguous i32 NEON reduction");
                check(saw_i64, "AArch64 optimizer forms contiguous i64 NEON reduction");
                auto encoded = forge::codegen::aarch64::encode(*lowered.module);
                check(encoded.ok(), "AArch64 automatic NEON reductions encode");
                if (encoded.ok() && encoded.functions.size() == 2U) {
                    bool saw_addv = false;
                    bool saw_addp = false;
                    for (const auto& function : encoded.functions) {
                        check(function.neon_vector_operation_count != 0U,
                              "AArch64 reduction reports native NEON ALU work");
                        for (std::size_t offset = 0; offset + 4U <= function.code.size(); offset += 4U) {
                            const auto word = static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(function.code[offset])) |
                                (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(function.code[offset + 1U])) << 8U) |
                                (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(function.code[offset + 2U])) << 16U) |
                                (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(function.code[offset + 3U])) << 24U);
                            saw_addv = saw_addv || word == 0x4EB1B800U;
                            saw_addp = saw_addp || word == 0x5EF1B800U;
                        }
                    }
                    check(saw_addv, "AArch64 i32 reduction emits ADDV s0, v0.4s");
                    check(saw_addp, "AArch64 i64 reduction emits ADDP d0, v0.2d");
                }
            }
        }
    }

    // Explicit packed chain and postfix-DAG forms use the same target-neutral
    // machine pseudos as x86, but evaluate directly in AArch64 Q registers.
    {
        const auto append_token = [](std::string& program, std::uint16_t token) {
            program.push_back(static_cast<char>(token & 0xffU));
            program.push_back(static_cast<char>((token >> 8U) & 0xffU));
        };
        forge::machine::Module packed_module;
        packed_module.name = "a64_packed_expressions";

        forge::machine::Function chain_function;
        chain_function.name = "chain";
        chain_function.argument_count = 5U;
        chain_function.argument_widths.assign(5U, 8U);
        chain_function.argument_classes.assign(5U, forge::machine::RegisterClass::integer);
        chain_function.register_count = 5U;
        chain_function.register_widths.assign(5U, 8U);
        chain_function.register_classes.assign(5U, forge::machine::RegisterClass::integer);
        forge::machine::Block chain_entry;
        chain_entry.name = "entry";
        for (std::uint32_t reg = 0; reg < 5U; ++reg) {
            forge::machine::Instruction argument;
            argument.opcode = forge::machine::Opcode::load_argument_i64;
            argument.result = reg;
            argument.argument_index = reg;
            chain_entry.instructions.push_back(argument);
        }
        forge::machine::Instruction chain;
        chain.opcode = forge::machine::Opcode::binary_i32_contiguous_chain;
        chain.inputs = {0U, 1U, 2U, 3U, 4U};
        chain.immediate = 4;
        chain.vector_bits = 128U;
        append_token(chain.symbol, static_cast<std::uint16_t>(forge::machine::Opcode::add_i32));
        append_token(chain.symbol, static_cast<std::uint16_t>(forge::machine::Opcode::xor_i32));
        append_token(chain.symbol, static_cast<std::uint16_t>(forge::machine::Opcode::sub_i32));
        chain_entry.instructions.push_back(chain);
        forge::machine::Instruction done;
        done.opcode = forge::machine::Opcode::return_void;
        chain_entry.instructions.push_back(done);
        chain_function.blocks.push_back(std::move(chain_entry));
        packed_module.functions.push_back(std::move(chain_function));

        forge::machine::Function dag_function;
        dag_function.name = "dag";
        dag_function.argument_count = 4U;
        dag_function.argument_widths.assign(4U, 8U);
        dag_function.argument_classes.assign(4U, forge::machine::RegisterClass::integer);
        dag_function.register_count = 4U;
        dag_function.register_widths.assign(4U, 8U);
        dag_function.register_classes.assign(4U, forge::machine::RegisterClass::integer);
        forge::machine::Block dag_entry;
        dag_entry.name = "entry";
        for (std::uint32_t reg = 0; reg < 4U; ++reg) {
            forge::machine::Instruction argument;
            argument.opcode = forge::machine::Opcode::load_argument_i64;
            argument.result = reg;
            argument.argument_index = reg;
            dag_entry.instructions.push_back(argument);
        }
        forge::machine::Instruction dag;
        dag.opcode = forge::machine::Opcode::binary_i32_contiguous_dag;
        dag.inputs = {0U, 1U, 2U, 3U};
        dag.immediate = 4;
        dag.vector_bits = 128U;
        append_token(dag.symbol, 0x8000U | 0U);
        append_token(dag.symbol, 0x8000U | 1U);
        append_token(dag.symbol, static_cast<std::uint16_t>(forge::machine::Opcode::add_i32));
        append_token(dag.symbol, 0x8000U | 2U);
        append_token(dag.symbol, static_cast<std::uint16_t>(forge::machine::Opcode::xor_i32));
        dag_entry.instructions.push_back(dag);
        dag_entry.instructions.push_back(done);
        dag_function.blocks.push_back(std::move(dag_entry));
        packed_module.functions.push_back(std::move(dag_function));

        check(forge::machine::verify_module(packed_module).empty(),
              "AArch64 packed chain/DAG fixtures verify");
        auto encoded = forge::codegen::aarch64::encode(packed_module);
        check(encoded.ok(), "AArch64 packed chain/DAG fixtures encode");
        if (encoded.ok() && encoded.functions.size() == 2U) {
            check(encoded.functions[0].neon_vector_operation_count == 3U,
                  "AArch64 packed chain emits three NEON ALU operations");
            check(encoded.functions[1].neon_vector_operation_count == 2U,
                  "AArch64 postfix DAG emits two NEON ALU operations");
        }
    }

    if (failures != 0) return EXIT_FAILURE;
    std::cout << "AArch64 codegen: scalar/AAPCS64/TLS relocation path PASS\n";
    return EXIT_SUCCESS;
}
