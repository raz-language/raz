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
#include "forge/platform/aarch64_immediate.hpp"
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
    {
        const auto and64 = forge::target::encode_aarch64_logical_immediate(0xffU, 64U);
        const auto or64 = forge::target::encode_aarch64_logical_immediate(0xff00U, 64U);
        const auto xor64 = forge::target::encode_aarch64_logical_immediate(0x0fU, 64U);
        const auto and32 = forge::target::encode_aarch64_logical_immediate(0xffU, 32U);
        check(and64 && and64->n == 1U && and64->immr == 0U && and64->imms == 7U,
              "AArch64 logical immediate encodes #0xff for 64-bit AND");
        check(or64 && or64->n == 1U && or64->immr == 56U && or64->imms == 7U,
              "AArch64 logical immediate encodes #0xff00 for 64-bit ORR");
        check(xor64 && xor64->n == 1U && xor64->immr == 0U && xor64->imms == 3U,
              "AArch64 logical immediate encodes #0xf for 64-bit EOR");
        check(and32 && and32->n == 0U && and32->immr == 0U && and32->imms == 7U,
              "AArch64 logical immediate encodes #0xff for 32-bit AND");
        check(!forge::target::encode_aarch64_logical_immediate(0x12345U, 64U),
              "AArch64 logical immediate rejects a non-bitmask constant");
    }
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
    %mask = const i64 255
    %masked = and i64 %selected %mask
    %or_mask = const i64 65280
    %ored = or i64 %masked %or_mask
    %xor_mask = const i64 15
    %xored = xor i64 %ored %xor_mask
    %illegal_mask = const i64 74565
    %kept = and i64 %xored %illegal_mask
    return %kept
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
            std::size_t logical_immediate_count = 0U;
            bool saw_illegal_logical_register = false;
            for (const auto& block : immediate_lowered.module->functions.front().blocks) {
                for (const auto& instruction : block.instructions) {
                    saw_immediate = saw_immediate || instruction.symbol == "$imm";
                    saw_compare_immediate = saw_compare_immediate || instruction.symbol == "$cmpimm";
                    if ((instruction.opcode == forge::machine::Opcode::and_i64 ||
                         instruction.opcode == forge::machine::Opcode::or_i64 ||
                         instruction.opcode == forge::machine::Opcode::xor_i64) &&
                        instruction.symbol == "$imm")
                        ++logical_immediate_count;
                    saw_illegal_logical_register = saw_illegal_logical_register ||
                        (instruction.opcode == forge::machine::Opcode::and_i64 &&
                         instruction.symbol != "$imm");
                }
            }
            check(saw_immediate, "AArch64 lowering selects arithmetic/shift immediate pseudos");
            check(saw_compare_immediate, "AArch64 lowering selects compare immediate pseudos");
            check(logical_immediate_count == 3U,
                  "AArch64 lowering selects all legal AND/OR/XOR logical immediates");
            check(saw_illegal_logical_register,
                  "AArch64 lowering leaves a non-encodable logical mask in a register");
            auto immediate_encoded = forge::codegen::aarch64::encode(*immediate_lowered.module);
            check(immediate_encoded.ok(), "AArch64 immediate-selected function encodes");
            if (immediate_encoded.ok() && !immediate_encoded.functions.empty()) {
                const auto& stats = immediate_encoded.functions.front();
                check(stats.immediate_form_count >= 7U,
                      "AArch64 encoder emits native arithmetic/shift/cmp/logical immediate forms");
                check(stats.elided_dead_constant_count >= 7U,
                      "AArch64 constants folded into immediate forms are not materialized into registers");
            }
        }
    }


    const std::string aggregate_boundary_source = R"(
module @a64_aggregate_boundary {
  struct @Trio32 { a: i32, b: i32, c: i32 }
  struct @Hfa3 { a: f32, b: f32, c: f32 }
  extern c func @take_trio(%x0: i64, %x1: i64, %x2: i64, %x3: i64, %x4: i64, %x5: i64, %x6: i64, %value: owned struct @Trio32, %tail: i64) -> i64
  extern c func @take_hfa(%v0: f32, %v1: f32, %v2: f32, %v3: f32, %v4: f32, %v5: f32, %v6: f32, %value: owned struct @Hfa3, %tail: f32) -> f32

  func @call_trio(%x0: i64, %x1: i64, %x2: i64, %x3: i64, %x4: i64, %x5: i64, %x6: i64, %tail: i64) -> i64 {
  entry:
    %value = stack.alloc.struct ptr @Trio32
    %result = call i64 @take_trio(%x0, %x1, %x2, %x3, %x4, %x5, %x6, %value, %tail)
    return %result
  }

  func @call_hfa(%v0: f32, %v1: f32, %v2: f32, %v3: f32, %v4: f32, %v5: f32, %v6: f32, %tail: f32) -> f32 {
  entry:
    %value = stack.alloc.struct ptr @Hfa3
    %result = call f32 @take_hfa(%v0, %v1, %v2, %v3, %v4, %v5, %v6, %value, %tail)
    return %result
  }
}
)";
    auto aggregate_boundary_parsed = forge::ir::parse_module(aggregate_boundary_source);
    check(aggregate_boundary_parsed.ok(), "AAPCS64 aggregate-boundary IR parses");
    if (aggregate_boundary_parsed.ok()) {
        check(forge::ir::verify_module(*aggregate_boundary_parsed.module).empty(),
              "AAPCS64 aggregate-boundary IR verifies");
        auto aggregate_boundary_lowered = forge::machine::lower_module(
            *aggregate_boundary_parsed.module, {forge::machine::TargetArchitecture::aarch64});
        check(aggregate_boundary_lowered.ok(), "AAPCS64 aggregate-boundary IR lowers");
        if (aggregate_boundary_lowered.ok()) {
            const auto* trio_call = static_cast<const forge::machine::Instruction*>(nullptr);
            const auto* hfa_call = static_cast<const forge::machine::Instruction*>(nullptr);
            for (const auto& function : aggregate_boundary_lowered.module->functions) {
                for (const auto& block : function.blocks) {
                    for (const auto& instruction : block.instructions) {
                        if (instruction.symbol == "take_trio") trio_call = &instruction;
                        if (instruction.symbol == "take_hfa") hfa_call = &instruction;
                    }
                }
            }
            check(trio_call != nullptr && trio_call->argument_group_sizes.size() == trio_call->inputs.size(),
                  "AAPCS64 direct composite call retains argument-group metadata");
            if (trio_call) {
                check(trio_call->inputs.size() == 10U && trio_call->argument_group_sizes[7] == 2U &&
                      trio_call->argument_group_sizes[8] == 0U && trio_call->argument_group_sizes[9] == 1U,
                      "AAPCS64 12-byte composite remains one two-piece machine argument");
            }
            check(hfa_call != nullptr && hfa_call->argument_group_sizes.size() == hfa_call->inputs.size(),
                  "AAPCS64 HFA call retains argument-group metadata");
            if (hfa_call) {
                check(hfa_call->inputs.size() == 11U && hfa_call->argument_group_sizes[7] == 3U &&
                      hfa_call->argument_group_sizes[8] == 0U && hfa_call->argument_group_sizes[9] == 0U &&
                      hfa_call->argument_group_sizes[10] == 1U,
                      "AAPCS64 three-member HFA remains one three-piece machine argument");
            }

            auto aggregate_boundary_encoded = forge::codegen::aarch64::encode(*aggregate_boundary_lowered.module);
            check(aggregate_boundary_encoded.ok(), "AAPCS64 aggregate-boundary functions encode");
            if (aggregate_boundary_encoded.ok()) {
                const auto find_function = [&](const char* name) -> const forge::codegen::aarch64::EncodedFunction* {
                    const auto it = std::find_if(aggregate_boundary_encoded.functions.begin(),
                                                 aggregate_boundary_encoded.functions.end(),
                        [&](const auto& function) { return function.name == name; });
                    return it == aggregate_boundary_encoded.functions.end() ? nullptr : &*it;
                };
                const auto* trio = find_function("call_trio");
                const auto* hfa = find_function("call_hfa");
                check(trio != nullptr && trio->abi_register_argument_count == 7U &&
                      trio->abi_stack_argument_count == 3U,
                      "AAPCS64 does not split a two-piece composite across x7 and the stack");
                check(hfa != nullptr && hfa->abi_register_argument_count == 7U &&
                      hfa->abi_stack_argument_count == 4U,
                      "AAPCS64 does not split a three-member HFA across v7 and the stack");
            }
        }
    }

    const std::string variadic_source = R"(
module @a64_variadic {
  extern variadic c func @variadic_sink(%fixed: i64) -> i64
  variadic c signature @VariadicCallback(%fixed: i64) -> i64
  func @call_variadic(%fixed: i64, %single: f32, %wide: f64, %tail: i64) -> i64 {
  entry:
    %result = call i64 @variadic_sink(%fixed, %single, %wide, %tail)
    return %result
  }
  func @call_variadic_indirect(%target: ptr, %fixed: i64, %single: f32, %wide: f64, %tail: i64) -> i64 {
  entry:
    %result = call.indirect i64 %target as @VariadicCallback(%fixed, %single, %wide, %tail)
    return %result
  }
}
)";
    auto variadic_parsed = forge::ir::parse_module(variadic_source);
    check(variadic_parsed.ok(), "AArch64 variadic IR parses");
    if (variadic_parsed.ok()) {
        check(forge::ir::verify_module(*variadic_parsed.module).empty(),
              "AArch64 variadic IR verifies");
        auto variadic_lowered = forge::machine::lower_module(
            *variadic_parsed.module, {forge::machine::TargetArchitecture::aarch64});
        check(variadic_lowered.ok(), "AArch64 variadic IR lowers");
        if (variadic_lowered.ok()) {
            const auto& function = variadic_lowered.module->functions.front();
            const forge::machine::Instruction* call = nullptr;
            std::size_t float_extensions = 0U;
            for (const auto& block : function.blocks) {
                for (const auto& instruction : block.instructions) {
                    if (instruction.opcode == forge::machine::Opcode::float_extend) ++float_extensions;
                    if (instruction.symbol == "variadic_sink") call = &instruction;
                }
            }
            check(call != nullptr && call->variadic_call && call->variadic_named_input_count == 1U,
                  "AArch64 variadic lowering preserves the named/anonymous boundary");
            check(float_extensions == 1U,
                  "C variadic lowering promotes an anonymous f32 argument to f64");
            if (call && call->inputs.size() == 4U) {
                const auto promoted = call->inputs[1];
                check(promoted < function.register_widths.size() && function.register_widths[promoted] == 8U &&
                      promoted < function.register_classes.size() &&
                      function.register_classes[promoted] == forge::machine::RegisterClass::floating,
                      "C variadic f32 promotion reaches the call as an eight-byte FP value");
            } else {
                check(false, "AArch64 variadic call keeps four machine arguments");
            }

            const auto& indirect_function = variadic_lowered.module->functions[1];
            const forge::machine::Instruction* indirect_call = nullptr;
            std::size_t indirect_float_extensions = 0U;
            for (const auto& block : indirect_function.blocks) {
                for (const auto& instruction : block.instructions) {
                    if (instruction.opcode == forge::machine::Opcode::float_extend) ++indirect_float_extensions;
                    if (instruction.opcode == forge::machine::Opcode::call_indirect_i64) indirect_call = &instruction;
                }
            }
            check(indirect_call != nullptr && indirect_call->variadic_call &&
                  indirect_call->variadic_named_input_count == 1U && indirect_call->inputs.size() == 5U,
                  "typed indirect variadic lowering preserves the named/anonymous boundary");
            check(indirect_float_extensions == 1U,
                  "typed indirect C variadic lowering promotes anonymous f32 to f64");

            auto generic_variadic = forge::codegen::aarch64::encode(
                *variadic_lowered.module, forge::codegen::aarch64::Abi::aapcs64);
            check(generic_variadic.ok(), "generic AAPCS64 variadic call encodes");
            if (generic_variadic.ok() && generic_variadic.functions.size() == 2U) {
                check(generic_variadic.functions[0].abi_register_argument_count == 4U &&
                      generic_variadic.functions[0].abi_stack_argument_count == 0U,
                      "generic AAPCS64 keeps variadic scalar arguments in available registers");
                check(generic_variadic.functions[1].abi_register_argument_count == 4U &&
                      generic_variadic.functions[1].abi_stack_argument_count == 0U,
                      "generic AAPCS64 keeps typed indirect variadic arguments in available registers");
            }

            auto darwin_variadic = forge::codegen::aarch64::encode(
                *variadic_lowered.module, forge::codegen::aarch64::Abi::darwin);
            check(darwin_variadic.ok(), "Darwin arm64 variadic call encodes");
            if (darwin_variadic.ok() && darwin_variadic.functions.size() == 2U) {
                check(darwin_variadic.functions[0].abi_register_argument_count == 1U &&
                      darwin_variadic.functions[0].abi_stack_argument_count == 3U,
                      "Darwin arm64 places every anonymous variadic argument on the stack");
                check(darwin_variadic.functions[1].abi_register_argument_count == 1U &&
                      darwin_variadic.functions[1].abi_stack_argument_count == 3U,
                      "Darwin arm64 stacks the typed-indirect anonymous variadic tail");
            }
        }
    }

    const std::string darwin_stack_source = R"(
module @a64_darwin_stack {
  extern c func @narrow11(%a0: i8, %a1: i8, %a2: i8, %a3: i8, %a4: i8, %a5: i8, %a6: i8, %a7: i8, %a8: i8, %a9: i8, %a10: i8) -> i32
  func @call_narrow11(%a0: i8, %a1: i8, %a2: i8, %a3: i8, %a4: i8, %a5: i8, %a6: i8, %a7: i8, %a8: i8, %a9: i8, %a10: i8) -> i32 {
  entry:
    %result = call i32 @narrow11(%a0, %a1, %a2, %a3, %a4, %a5, %a6, %a7, %a8, %a9, %a10)
    return %result
  }
}
)";
    auto darwin_stack_parsed = forge::ir::parse_module(darwin_stack_source);
    check(darwin_stack_parsed.ok(), "Darwin narrow-stack IR parses");
    if (darwin_stack_parsed.ok()) {
        check(forge::ir::verify_module(*darwin_stack_parsed.module).empty(),
              "Darwin narrow-stack IR verifies");
        auto darwin_stack_lowered = forge::machine::lower_module(
            *darwin_stack_parsed.module, {forge::machine::TargetArchitecture::aarch64});
        check(darwin_stack_lowered.ok(), "Darwin narrow-stack IR lowers");
        if (darwin_stack_lowered.ok()) {
            const auto& function = darwin_stack_lowered.module->functions.front();
            check(function.argument_widths.size() == 11U &&
                  std::all_of(function.argument_widths.begin(), function.argument_widths.end(),
                              [](std::uint8_t width) { return width == 1U; }),
                  "AArch64 lowering retains one-byte ABI widths for i8 parameters");
            const forge::machine::Instruction* call = nullptr;
            for (const auto& block : function.blocks)
                for (const auto& instruction : block.instructions)
                    if (instruction.symbol == "narrow11") call = &instruction;
            check(call != nullptr && call->argument_widths.size() == call->inputs.size() &&
                  std::all_of(call->argument_widths.begin(), call->argument_widths.end(),
                              [](std::uint8_t width) { return width == 1U; }),
                  "AArch64 call lowering retains one-byte ABI widths for fixed i8 arguments");

            auto generic_stack = forge::codegen::aarch64::encode(
                *darwin_stack_lowered.module, forge::codegen::aarch64::Abi::aapcs64);
            auto darwin_stack = forge::codegen::aarch64::encode(
                *darwin_stack_lowered.module, forge::codegen::aarch64::Abi::darwin);
            check(generic_stack.ok() && darwin_stack.ok(),
                  "generic and Darwin narrow-stack calls encode");
            if (generic_stack.ok() && darwin_stack.ok() &&
                !generic_stack.functions.empty() && !darwin_stack.functions.empty()) {
                check(generic_stack.functions.front().abi_stack_argument_count == 3U &&
                      darwin_stack.functions.front().abi_stack_argument_count == 3U,
                      "both AArch64 ABIs spill three i8 arguments after x0-x7");
                check(generic_stack.functions.front().abi_outgoing_stack_bytes == 32U,
                      "generic AAPCS64 rounds three narrow stack arguments to 8-byte slots");
                check(darwin_stack.functions.front().abi_outgoing_stack_bytes == 16U,
                      "Darwin arm64 tightly packs three fixed i8 stack arguments before final SP alignment");
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
