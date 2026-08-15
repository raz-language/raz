// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/codegen/x86_64/encoder.hpp"
#include "forge/jit/engine.hpp"

#include <algorithm>
#include "forge/ir/parser.hpp"
#include "forge/ir/verifier.hpp"
#include "forge/machine/lower.hpp"
#include "forge/machine/module.hpp"
#include "forge/machine/optimize.hpp"
#include "forge/machine/register_allocation.hpp"
#include "forge/machine/verifier.hpp"

#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

#if defined(__unix__) && (defined(__x86_64__) || defined(__amd64__))
#include <sys/mman.h>
#include <unistd.h>
#endif

extern "C" int forge_test_host_add(int left, int right) { return left + right; }
static int forge_test_recorded = 0;
extern "C" void forge_test_host_record(int value) { forge_test_recorded = value; }

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

#if defined(__unix__) && (defined(__x86_64__) || defined(__amd64__))
template <typename Function>
Function make_executable(const std::vector<std::byte>& code, void*& memory, std::size_t& allocation_size) {
    const auto page_size = static_cast<std::size_t>(::sysconf(_SC_PAGESIZE));
    allocation_size = ((code.size() + page_size - 1) / page_size) * page_size;
    memory = ::mmap(nullptr, allocation_size, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    require(memory != MAP_FAILED, "mmap failed");
    std::memcpy(memory, code.data(), code.size());
    require(::mprotect(memory, allocation_size, PROT_READ | PROT_EXEC) == 0, "mprotect failed");
    return reinterpret_cast<Function>(memory);
}
#endif

int main() {
    try {
        constexpr auto source = R"(module @native {
func @calculate(%left: i32, %right: i32) -> i32 {
entry:
  %scale = const i32 3
  %sum = add i32 %left %right
  %result = mul i32 %sum %scale
  return %result
}
func @maximum(%left: i32, %right: i32) -> i32 {
entry:
  %greater = cmp.gt i32 %left %right
  branch %greater, selected(%left), selected(%right)
selected(%value: i32):
  return %value
}
func @sum_to(%limit: i32) -> i32 {
entry:
  %zero = const i32 0
  %one = const i32 1
  jump loop(%zero, %zero)
loop(%index: i32, %total: i32):
  %keep_going = cmp.lt i32 %index %limit
  branch %keep_going, body(%index, %total), exit(%total)
body(%current: i32, %running: i32):
  %next_total = add i32 %running %current
  %next_index = add i32 %current %one
  jump loop(%next_index, %next_total)
exit(%result: i32):
  return %result
}
func @integer_ops(%left: i32, %right: i32) -> i32 {
entry:
  %quotient = div.signed i32 %left %right
  %remainder = rem.signed i32 %left %right
  %one = const i32 1
  %shifted = shl i32 %quotient %one
  %mixed = xor i32 %shifted %remainder
  return %mixed
}
func @unsigned_max(%left: i32, %right: i32) -> i32 {
entry:
  %greater = cmp.ugt i32 %left %right
  branch %greater, selected(%left), selected(%right)
selected(%value: i32):
  return %value
}
func @stack_memory(%left: i32, %right: i32) -> i32 {
entry:
  %buffer = stack.alloc ptr 8
  %second = ptr.offset ptr %buffer 4
  store i32 %left %buffer
  store i32 %right %second
  %loaded_left = load i32 %buffer
  %loaded_right = load i32 %second
  %sum = add i32 %loaded_left %loaded_right
  return %sum
}
func @sum_eight(%a: i32, %b: i32, %c: i32, %d: i32, %e: i32, %f: i32, %g: i32, %h: i32) -> i32 {
entry:
  %ab = add i32 %a %b
  %cd = add i32 %c %d
  %ef = add i32 %e %f
  %gh = add i32 %g %h
  %abcd = add i32 %ab %cd
  %efgh = add i32 %ef %gh
  %all = add i32 %abcd %efgh
  return %all
}

})";
        auto parsed = forge::ir::parse_module(source);
        require(parsed.ok(), "codegen fixture did not parse");
        require(forge::ir::verify_module(*parsed.module).empty(), "codegen fixture did not verify");
        auto lowered = forge::machine::lower_module(*parsed.module);
        require(lowered.ok(), "IR to machine lowering failed");
        require(lowered.module->functions.size() == 7, "wrong machine function count");
        require(forge::machine::verify_module(*lowered.module).empty(), "valid machine IR failed verification");
        {
            constexpr auto invalid_memory = R"(module @bad {
func @overflow() -> i32 {
entry:
  %buffer = stack.alloc ptr 4
  %past_end = ptr.offset ptr %buffer 4
  %value = load i32 %past_end
  return %value
}
})";
            auto invalid_parsed = forge::ir::parse_module(invalid_memory);
            require(invalid_parsed.ok(), "invalid-memory fixture did not parse");
            auto invalid_lowered = forge::machine::lower_module(*invalid_parsed.module);
            require(!invalid_lowered.ok(), "out-of-bounds stack load was accepted");
        }
        {
            auto malformed = *lowered.module;
            malformed.functions.front().blocks.front().instructions.front().result = malformed.functions.front().register_count;
            require(!forge::machine::verify_module(malformed).empty(), "machine verifier accepted an out-of-range result");
        }
        const auto machine_text = forge::machine::print_module(*lowered.module);
        require(machine_text.find("mul_i32") != std::string::npos, "machine IR lost multiplication");
        require(machine_text.find("cmp_gt_i32") != std::string::npos, "machine IR lost comparison");
        require(machine_text.find("branch_i1") != std::string::npos, "machine IR lost conditional branch");
        require(machine_text.find("loop(") != std::string::npos, "machine IR lost block parameters");
        require(machine_text.find("load_stack_i32") != std::string::npos, "machine IR lost stack load");
        require(machine_text.find("store_stack_i32") != std::string::npos, "machine IR lost stack store");

        const auto baseline = forge::machine::allocate_stack_slots(lowered.module->functions.front());
        require(baseline.ok(), "baseline register allocation failed");
        require(baseline.offsets.size() == lowered.module->functions.front().register_count,
                "not every virtual register received a stack home");

        const auto allocation = forge::machine::allocate_linear_scan(lowered.module->functions.front());
        require(allocation.ok(), "linear-scan register allocation failed");
        require(allocation.locations.size() == lowered.module->functions.front().register_count,
                "linear scan did not assign every virtual register");
        require(allocation.physical_count != 0, "linear scan did not use physical registers");
        require(allocation.caller_saved_allocation_count != 0,
                "short-lived values did not prefer caller-saved registers");
        require(allocation.peak_integer_pressure != 0,
                "allocator did not report integer register pressure");

        const auto loop_allocation = forge::machine::allocate_linear_scan(lowered.module->functions[2]);
        require(loop_allocation.ok(), "loop liveness allocation failed");
        require(loop_allocation.intervals.size() == lowered.module->functions[2].register_count,
                "loop liveness did not produce complete intervals");
        require(lowered.module->functions[2].machine_compare_branches_fused != 0,
                "loop comparison was not fused into its branch");

        forge::machine::Function floating_leaf;
        floating_leaf.name = "floating_leaf";
        floating_leaf.register_count = 7;
        floating_leaf.register_widths = {8, 8, 8, 8, 8, 8, 8};
        floating_leaf.register_classes.assign(7, forge::machine::RegisterClass::floating);
        forge::machine::Block floating_entry;
        floating_entry.name = "entry";
        const auto make_float_instruction = [](forge::machine::Opcode opcode,
                                               forge::machine::VirtualRegister result,
                                               std::vector<forge::machine::VirtualRegister> inputs,
                                               std::int64_t immediate = 0) {
            forge::machine::Instruction instruction;
            instruction.opcode = opcode;
            instruction.result = result;
            instruction.inputs = std::move(inputs);
            instruction.immediate = immediate;
            return instruction;
        };
        floating_entry.instructions = {
            make_float_instruction(forge::machine::Opcode::load_immediate_f64, 0, {}, 0x3ff0000000000000LL),
            make_float_instruction(forge::machine::Opcode::load_immediate_f64, 1, {}, 0x4000000000000000LL),
            make_float_instruction(forge::machine::Opcode::load_immediate_f64, 2, {}, 0x4008000000000000LL),
            make_float_instruction(forge::machine::Opcode::load_immediate_f64, 3, {}, 0x4010000000000000LL),
            make_float_instruction(forge::machine::Opcode::add_f64, 4, {0, 1}),
            make_float_instruction(forge::machine::Opcode::mul_f64, 5, {2, 3}),
            make_float_instruction(forge::machine::Opcode::add_f64, 6, {4, 5}),
            make_float_instruction(forge::machine::Opcode::return_f64, 0, {6}),
        };
        floating_leaf.blocks.push_back(std::move(floating_entry));
        const auto floating_baseline = forge::machine::allocate_stack_slots(floating_leaf);
        const auto floating_allocation = forge::machine::allocate_linear_scan(floating_leaf);
        require(floating_allocation.ok(), "floating linear-scan allocation failed");
        bool used_xmm = false;
        std::vector<forge::machine::FloatingPhysicalRegister> used_floating_registers;
        for (const auto& location : floating_allocation.locations) {
            used_xmm = used_xmm || location.kind == forge::machine::LocationKind::floating_register;
            if (location.kind == forge::machine::LocationKind::floating_register &&
                std::find(used_floating_registers.begin(), used_floating_registers.end(), location.floating) == used_floating_registers.end())
                used_floating_registers.push_back(location.floating);
        }

        require(used_xmm, "floating linear scan did not keep any value in XMM");
        require(used_floating_registers.size() >= 4, "floating linear scan did not use the four-register XMM pool");
        require(floating_allocation.spill_count < floating_leaf.register_count,
                "floating linear scan still spilled every virtual register");
        require(floating_allocation.frame_size < floating_baseline.frame_size,
                "floating linear scan did not reduce the stack frame");

        // Floating values that are consumed by a call are not live across that
        // call.  The outgoing XMM parallel-copy planner can place them safely,
        // so they must remain eligible for register allocation instead of being
        // assigned synthetic stack homes merely because they are arguments.
        forge::machine::Function floating_call_arguments;
        floating_call_arguments.name = "floating_call_arguments";
        floating_call_arguments.register_count = 3;
        floating_call_arguments.register_widths = {8, 8, 8};
        floating_call_arguments.register_classes.assign(3, forge::machine::RegisterClass::floating);
        forge::machine::Block floating_call_entry;
        floating_call_entry.name = "entry";
        floating_call_entry.instructions.push_back(
            make_float_instruction(forge::machine::Opcode::load_immediate_f64, 0, {}, 0x3ff0000000000000LL));
        floating_call_entry.instructions.push_back(
            make_float_instruction(forge::machine::Opcode::load_immediate_f64, 1, {}, 0x4000000000000000LL));
        auto floating_call = make_float_instruction(forge::machine::Opcode::call_f64, 2, {0, 1});
        floating_call.symbol = "callee";
        floating_call_entry.instructions.push_back(std::move(floating_call));
        floating_call_entry.instructions.push_back(
            make_float_instruction(forge::machine::Opcode::return_f64, 0, {2}));
        floating_call_arguments.blocks.push_back(std::move(floating_call_entry));
        const auto floating_call_allocation = forge::machine::allocate_linear_scan(floating_call_arguments);
        require(floating_call_allocation.ok(), "floating call-argument allocation failed");
        require(floating_call_allocation.location(0).kind == forge::machine::LocationKind::floating_register &&
                floating_call_allocation.location(1).kind == forge::machine::LocationKind::floating_register,
                "floating values consumed by a call were unnecessarily forced to stack slots");

        forge::machine::Function weighted_pressure;
        weighted_pressure.name = "weighted_pressure";
        weighted_pressure.register_count = 10;
        weighted_pressure.register_widths.assign(10, 8);
        weighted_pressure.register_classes.assign(10, forge::machine::RegisterClass::integer);
        forge::machine::Block weighted_entry;
        weighted_entry.name = "entry";
        for (forge::machine::VirtualRegister reg = 0; reg < 9; ++reg)
            weighted_entry.instructions.push_back(
                make_float_instruction(forge::machine::Opcode::load_immediate_i64, reg, {}, reg + 1));
        const auto add_store = [&](forge::machine::VirtualRegister reg, std::int64_t offset) {
            auto instruction = make_float_instruction(forge::machine::Opcode::store_stack_i64, 0, {reg}, offset);
            weighted_entry.instructions.push_back(std::move(instruction));
        };
        add_store(1, 0);
        add_store(2, 8);
        add_store(3, 16);
        add_store(4, 24);
        add_store(5, 32);
        add_store(6, 40);
        add_store(7, 48);
        add_store(8, 56);
        add_store(9, 64);
        add_store(9, 72);
        add_store(9, 80);
        add_store(9, 88);
        weighted_entry.instructions.push_back(
            make_float_instruction(forge::machine::Opcode::return_i64, 0, {0}));
        weighted_pressure.local_stack_size = 96;
        weighted_pressure.blocks.push_back(std::move(weighted_entry));
        const auto weighted_allocation = forge::machine::allocate_linear_scan(weighted_pressure);
        require(weighted_allocation.ok(), "weighted pressure allocation failed");
        require(weighted_allocation.location(9).kind == forge::machine::LocationKind::physical_register,
                "hot integer value was spilled instead of retained");
        require(weighted_allocation.intervals[9].use_count >= 4,
                "hot integer use frequency was not recorded");
        require(weighted_allocation.intervals[9].spill_weight > weighted_allocation.intervals[1].spill_weight,
                "spill weighting did not distinguish hot and cold values");
        bool cold_value_spilled = false;
        for (forge::machine::VirtualRegister reg = 0; reg < 10; ++reg)
            cold_value_spilled = cold_value_spilled ||
                weighted_allocation.location(reg).kind == forge::machine::LocationKind::stack_slot ||
                weighted_allocation.location(reg).kind == forge::machine::LocationKind::rematerialized_integer;
        require(cold_value_spilled, "weighted allocator did not select a cold spill or rematerialization candidate");
        require(weighted_allocation.rematerialized_value_count > 0,
                "weighted allocator did not rematerialize any cold constants under pressure");
        require(weighted_allocation.spill_slot_count == 0,
                "rematerialized constant still consumed a physical spill slot");

        forge::machine::Function call_aware;
        call_aware.name = "call_aware";
        call_aware.argument_count = 1;
        call_aware.argument_widths = {8};
        call_aware.argument_classes = {forge::machine::RegisterClass::integer};
        call_aware.register_count = 3;
        call_aware.register_widths.assign(3, 8);
        call_aware.register_classes.assign(3, forge::machine::RegisterClass::integer);
        forge::machine::Block call_aware_entry;
        call_aware_entry.name = "entry";
        auto call_arg = make_float_instruction(forge::machine::Opcode::load_argument_i64, 0, {});
        call_arg.argument_index = 0;
        call_aware_entry.instructions.push_back(std::move(call_arg));
        auto call = make_float_instruction(forge::machine::Opcode::call_i64, 1, {});
        call.symbol = "external_value";
        call_aware_entry.instructions.push_back(std::move(call));
        call_aware_entry.instructions.push_back(
            make_float_instruction(forge::machine::Opcode::add_i64, 2, {0, 1}));
        call_aware_entry.instructions.push_back(
            make_float_instruction(forge::machine::Opcode::return_i64, 0, {2}));
        call_aware.blocks.push_back(std::move(call_aware_entry));
        const auto call_aware_allocation = forge::machine::allocate_linear_scan(call_aware);
        require(call_aware_allocation.ok(), "call-aware allocation failed");
        require(call_aware_allocation.call_crossing_interval_count >= 1,
                "allocator did not identify a call-crossing live interval");
        require(call_aware_allocation.location(0).kind == forge::machine::LocationKind::physical_register &&
                forge::machine::is_callee_saved(call_aware_allocation.location(0).physical),
                "call-crossing value was not assigned to a callee-saved register");
        require(call_aware_allocation.callee_saved_allocation_count >= 1,
                "allocator did not report its callee-saved allocation");

        // True transition-based splitting: five values remain live across one
        // call, but the ABI has only two callee-saved allocation registers.
        // Forge must spill the excess values immediately before the call,
        // reload them into new virtual registers afterward, and allocate the
        // pre/post-call pieces independently.
        forge::machine::Function transition_split;
        transition_split.name = "transition_split";
        transition_split.argument_count = 5;
        transition_split.argument_widths.assign(5, 8);
        transition_split.argument_classes.assign(5, forge::machine::RegisterClass::integer);
        transition_split.register_count = 10;
        transition_split.register_widths.assign(10, 8);
        transition_split.register_classes.assign(10, forge::machine::RegisterClass::integer);
        forge::machine::Block transition_entry;
        transition_entry.name = "entry";
        for (forge::machine::VirtualRegister reg = 0; reg < 5; ++reg) {
            auto argument = make_float_instruction(forge::machine::Opcode::load_argument_i64, reg, {});
            argument.argument_index = reg;
            transition_entry.instructions.push_back(std::move(argument));
        }
        auto transition_call = make_float_instruction(forge::machine::Opcode::call_i64, 5, {});
        transition_call.symbol = "external_value";
        transition_entry.instructions.push_back(std::move(transition_call));
        transition_entry.instructions.push_back(make_float_instruction(forge::machine::Opcode::add_i64, 6, {0, 1}));
        transition_entry.instructions.push_back(make_float_instruction(forge::machine::Opcode::add_i64, 7, {2, 3}));
        transition_entry.instructions.push_back(make_float_instruction(forge::machine::Opcode::add_i64, 8, {6, 7}));
        transition_entry.instructions.push_back(make_float_instruction(forge::machine::Opcode::add_i64, 9, {8, 4}));
        transition_entry.instructions.push_back(make_float_instruction(forge::machine::Opcode::return_i64, 0, {9}));
        transition_split.blocks.push_back(std::move(transition_entry));

        const auto split_stats = forge::machine::split_live_ranges_around_calls(transition_split);
        require(split_stats.split_values == 3, "allocator did not split excess call-crossing values");
        require(split_stats.transition_stores == 3 && split_stats.transition_loads == 3,
                "allocator did not insert balanced split transitions");
        require(transition_split.local_stack_size == 24,
                "split transition slots were not reserved in the local frame");
        require(transition_split.register_count == 13,
                "post-call split segments did not receive new virtual registers");
        forge::machine::Module transition_module;
        transition_module.name = "transition_split_module";
        transition_module.functions.push_back(transition_split);
        require(forge::machine::verify_module(transition_module).empty(),
                "transition-split machine function failed verification");
        const auto transition_allocation = forge::machine::allocate_linear_scan(transition_split);
        require(transition_allocation.ok(), "transition-split allocation failed");
        require(transition_allocation.call_crossing_interval_count == 2,
                "transition splitting did not reduce call-crossing pressure to the callee-saved capacity");
        std::uint32_t transition_store_count = 0;
        std::uint32_t transition_load_count = 0;
        for (const auto& instruction : transition_split.blocks.front().instructions) {
            transition_store_count += instruction.opcode == forge::machine::Opcode::store_stack_i64 ? 1U : 0U;
            transition_load_count += instruction.opcode == forge::machine::Opcode::load_stack_i64 ? 1U : 0U;
        }

        require(transition_store_count == 3 && transition_load_count == 3,
                "register-to-stack-to-register transitions are missing from machine IR");

        // Cross-block transition splitting: the call ends one block and the
        // continuation is a single-predecessor successor. Excess live values
        // must spill before the call and reload at continuation entry.
        forge::machine::Function edge_split;
        edge_split.name = "edge_split";
        edge_split.argument_count = 5;
        edge_split.argument_widths.assign(5, 8);
        edge_split.argument_classes.assign(5, forge::machine::RegisterClass::integer);
        edge_split.register_count = 10;
        edge_split.register_widths.assign(10, 8);
        edge_split.register_classes.assign(10, forge::machine::RegisterClass::integer);
        forge::machine::Block edge_entry;
        edge_entry.name = "entry";
        for (forge::machine::VirtualRegister reg = 0; reg < 5; ++reg) {
            auto argument = make_float_instruction(forge::machine::Opcode::load_argument_i64, reg, {});
            argument.argument_index = reg;
            edge_entry.instructions.push_back(std::move(argument));
        }
        auto edge_call = make_float_instruction(forge::machine::Opcode::call_i64, 5, {});
        edge_call.symbol = "external_value";
        edge_entry.instructions.push_back(std::move(edge_call));
        auto edge_jump = make_float_instruction(forge::machine::Opcode::jump, 0, {});
        edge_jump.successors = {{"continue", {}}};
        edge_entry.instructions.push_back(std::move(edge_jump));
        forge::machine::Block edge_continue;
        edge_continue.name = "continue";
        edge_continue.instructions.push_back(make_float_instruction(forge::machine::Opcode::add_i64, 6, {0, 1}));
        edge_continue.instructions.push_back(make_float_instruction(forge::machine::Opcode::add_i64, 7, {2, 3}));
        edge_continue.instructions.push_back(make_float_instruction(forge::machine::Opcode::add_i64, 8, {6, 7}));
        edge_continue.instructions.push_back(make_float_instruction(forge::machine::Opcode::add_i64, 9, {8, 4}));
        edge_continue.instructions.push_back(make_float_instruction(forge::machine::Opcode::return_i64, 0, {9}));
        edge_split.blocks.push_back(std::move(edge_entry));
        edge_split.blocks.push_back(std::move(edge_continue));
        const auto edge_stats = forge::machine::split_live_ranges_around_calls(edge_split);
        require(edge_stats.cross_block_split_values == 3,
                "allocator did not split excess values across the continuation edge");
        require(edge_split.blocks.front().instructions.size() >= 5 &&
                edge_split.blocks[1].instructions.size() >= 8,
                "cross-block transitions were not inserted at both edge boundaries");
        forge::machine::Module edge_module;
        edge_module.name = "edge_split_module";
        edge_module.functions.push_back(edge_split);
        require(forge::machine::verify_module(edge_module).empty(),
                "cross-block transition-split function failed verification");

        // Critical-edge merge repair: one branch crosses a call while another
        // reaches the same continuation directly. Forge must create an edge
        // block, reload there, and merge original/reloaded values through block
        // parameters instead of rewriting the shared continuation unsafely.
        forge::machine::Function critical_split;
        critical_split.name = "critical_split";
        critical_split.argument_count = 6;
        critical_split.argument_widths = {4, 8, 8, 8, 8, 8};
        critical_split.argument_classes.assign(6, forge::machine::RegisterClass::integer);
        critical_split.register_count = 12;
        critical_split.register_widths.assign(12, 8);
        critical_split.register_widths[0] = 4;
        critical_split.register_classes.assign(12, forge::machine::RegisterClass::integer);

        forge::machine::Block critical_entry;
        critical_entry.name = "entry";
        for (forge::machine::VirtualRegister reg = 0; reg < 6; ++reg) {
            auto argument = make_float_instruction(
                reg == 0 ? forge::machine::Opcode::load_argument : forge::machine::Opcode::load_argument_i64,
                reg, {});
            argument.argument_index = reg;
            critical_entry.instructions.push_back(std::move(argument));
        }
        auto critical_branch = make_float_instruction(forge::machine::Opcode::branch_i1, 0, {0});
        critical_branch.successors = {{"with.call", {}}, {"without.call", {}}};
        critical_entry.instructions.push_back(std::move(critical_branch));

        forge::machine::Block with_call;
        with_call.name = "with.call";
        auto critical_call = make_float_instruction(forge::machine::Opcode::call_i64, 6, {});
        critical_call.symbol = "external_value";
        with_call.instructions.push_back(std::move(critical_call));
        auto with_call_jump = make_float_instruction(forge::machine::Opcode::jump, 0, {});
        with_call_jump.successors = {{"merge", {}}};
        with_call.instructions.push_back(std::move(with_call_jump));

        forge::machine::Block without_call;
        without_call.name = "without.call";
        auto without_call_jump = make_float_instruction(forge::machine::Opcode::jump, 0, {});
        without_call_jump.successors = {{"merge", {}}};
        without_call.instructions.push_back(std::move(without_call_jump));

        forge::machine::Block critical_merge;
        critical_merge.name = "merge";
        critical_merge.instructions.push_back(make_float_instruction(forge::machine::Opcode::add_i64, 7, {1, 2}));
        critical_merge.instructions.push_back(make_float_instruction(forge::machine::Opcode::add_i64, 8, {3, 4}));
        critical_merge.instructions.push_back(make_float_instruction(forge::machine::Opcode::add_i64, 9, {7, 8}));
        critical_merge.instructions.push_back(make_float_instruction(forge::machine::Opcode::add_i64, 10, {9, 5}));
        critical_merge.instructions.push_back(make_float_instruction(forge::machine::Opcode::add_i64, 11, {10, 6}));
        critical_merge.instructions.push_back(make_float_instruction(forge::machine::Opcode::return_i64, 0, {11}));

        critical_split.blocks.push_back(std::move(critical_entry));
        critical_split.blocks.push_back(std::move(with_call));
        critical_split.blocks.push_back(std::move(without_call));
        critical_split.blocks.push_back(std::move(critical_merge));
        const auto critical_stats = forge::machine::split_live_ranges_around_calls(critical_split);
        require(critical_stats.critical_edge_split_values == 3,
                "critical-edge splitter did not split excess call-crossing values");
        require(critical_stats.critical_edge_blocks == 1 && critical_stats.merge_parameters == 3,
                "critical-edge splitter did not create the expected edge block and merge parameters");
        require(critical_split.blocks.size() == 5 && critical_split.blocks[3].parameters.size() == 3,
                "critical-edge merge repair did not update the machine CFG");
        forge::machine::Module critical_module;
        critical_module.name = "critical_split_module";
        critical_module.functions.push_back(critical_split);
        require(forge::machine::verify_module(critical_module).empty(),
                "critical-edge transition-split function failed verification");

        // Interleave two mutually exclusive CFG arms so their bounding live
        // intervals overlap even though their real liveness segments do not.
        // The segmented recovery stage must reuse the same physical registers
        // instead of spilling the second arm.
        forge::machine::Function segmented_cfg;
        segmented_cfg.name = "segmented_cfg";
        segmented_cfg.argument_count = 1;
        segmented_cfg.argument_widths = {4};
        segmented_cfg.argument_classes = {forge::machine::RegisterClass::integer};
        segmented_cfg.register_count = 18;
        segmented_cfg.register_widths.assign(18, 8);
        segmented_cfg.register_widths[0] = 4;
        segmented_cfg.register_classes.assign(18, forge::machine::RegisterClass::integer);

        forge::machine::Block segmented_entry;
        segmented_entry.name = "entry";
        auto segmented_condition = make_float_instruction(forge::machine::Opcode::load_argument, 0, {});
        segmented_condition.argument_index = 0;
        segmented_entry.instructions.push_back(std::move(segmented_condition));
        auto segmented_branch = make_float_instruction(forge::machine::Opcode::branch_i1, 0, {0});
        segmented_branch.successors = {{"left.def", {}}, {"right.def", {}}};
        segmented_entry.instructions.push_back(std::move(segmented_branch));

        forge::machine::Block left_def;
        left_def.name = "left.def";
        for (forge::machine::VirtualRegister reg = 1; reg <= 4; ++reg)
            left_def.instructions.push_back(make_float_instruction(
                forge::machine::Opcode::load_immediate_i64, reg, {}, static_cast<std::int64_t>(reg)));
        auto left_jump = make_float_instruction(forge::machine::Opcode::jump, 0, {});
        left_jump.successors = {{"left.use", {}}};
        left_def.instructions.push_back(std::move(left_jump));

        forge::machine::Block right_def;
        right_def.name = "right.def";
        for (forge::machine::VirtualRegister reg = 5; reg <= 8; ++reg)
            right_def.instructions.push_back(make_float_instruction(
                forge::machine::Opcode::load_immediate_i64, reg, {}, static_cast<std::int64_t>(reg)));
        auto right_jump = make_float_instruction(forge::machine::Opcode::jump, 0, {});
        right_jump.successors = {{"right.use", {}}};
        right_def.instructions.push_back(std::move(right_jump));

        forge::machine::Block left_use;
        left_use.name = "left.use";
        left_use.instructions = {
            make_float_instruction(forge::machine::Opcode::add_i64, 9, {1, 2}),
            make_float_instruction(forge::machine::Opcode::add_i64, 10, {3, 4}),
            make_float_instruction(forge::machine::Opcode::add_i64, 11, {9, 10}),
            make_float_instruction(forge::machine::Opcode::return_i64, 0, {11}),
        };

        forge::machine::Block right_use;
        right_use.name = "right.use";
        right_use.instructions = {
            make_float_instruction(forge::machine::Opcode::add_i64, 12, {5, 6}),
            make_float_instruction(forge::machine::Opcode::add_i64, 13, {7, 8}),
            make_float_instruction(forge::machine::Opcode::add_i64, 14, {12, 13}),
            make_float_instruction(forge::machine::Opcode::return_i64, 0, {14}),
        };

        // Keep the block order deliberately interleaved.
        segmented_cfg.blocks = {std::move(segmented_entry), std::move(left_def), std::move(right_def),
                                std::move(left_use), std::move(right_use)};
        const auto segmented_allocation = forge::machine::allocate_linear_scan(segmented_cfg);
        require(segmented_allocation.ok(), "segmented CFG allocation failed");
        require(segmented_allocation.segmented_interval_count >= 8,
                "allocator did not record segmented live intervals");
        require(segmented_allocation.live_range_hole_count >= 8,
                "allocator did not record CFG live-range holes");
        std::uint32_t recovered_right_values = 0;
        for (forge::machine::VirtualRegister reg = 5; reg <= 8; ++reg)
            recovered_right_values += segmented_allocation.location(reg).kind ==
                                      forge::machine::LocationKind::physical_register ? 1U : 0U;
        require(recovered_right_values >= 2,
                "mutually exclusive right-arm values did not reuse registers");


        constexpr auto floating_comparison_source = R"(module @floating_classification {
func @comparison() -> i1 {
entry:
  %left = const f64 3.5
  %right = const f64 4.5
  %result = cmp.lt f64 %left %right
  return %result
}
})";
        auto floating_comparison_parse = forge::ir::parse_module(floating_comparison_source);
        require(floating_comparison_parse.module.has_value(), "floating comparison parse failed");
        auto floating_comparison_lowered = forge::machine::lower_module(*floating_comparison_parse.module);
        require(floating_comparison_lowered.module.has_value(), "floating comparison lowering failed");
        const auto& comparison_function = floating_comparison_lowered.module->functions.front();
        const auto& comparison_instruction = comparison_function.blocks.front().instructions.at(2);
        require(comparison_instruction.opcode == forge::machine::Opcode::cmp_lt_f64,
                "floating comparison lowered to wrong opcode");
        require(comparison_function.register_classes[comparison_instruction.result] == forge::machine::RegisterClass::integer,
                "floating comparison result was not classified as integer");

        auto invalid_comparison_module = *floating_comparison_lowered.module;
        invalid_comparison_module.functions.front().register_classes[comparison_instruction.result] =
            forge::machine::RegisterClass::floating;
        const auto invalid_comparison_diagnostics = forge::machine::verify_module(invalid_comparison_module);
        require(!invalid_comparison_diagnostics.empty(),
                "machine verifier accepted a floating comparison with an XMM-class result");

        forge::machine::Function copy_chain;
        copy_chain.name = "copy_chain";
        copy_chain.register_count = 6;
        copy_chain.register_widths = {8, 8, 8, 8, 8, 8};
        copy_chain.register_classes = {
            forge::machine::RegisterClass::integer, forge::machine::RegisterClass::integer,
            forge::machine::RegisterClass::integer, forge::machine::RegisterClass::floating,
            forge::machine::RegisterClass::floating, forge::machine::RegisterClass::floating};
        forge::machine::Block copy_entry;
        copy_entry.name = "entry";
        copy_entry.instructions = {
            make_float_instruction(forge::machine::Opcode::load_immediate_i64, 0, {}, 42),
            make_float_instruction(forge::machine::Opcode::copy, 1, {0}),
            make_float_instruction(forge::machine::Opcode::copy, 2, {1}),
            make_float_instruction(forge::machine::Opcode::load_immediate_f64, 3, {}, 0x4008000000000000LL),
            make_float_instruction(forge::machine::Opcode::copy_f64, 4, {3}),
            make_float_instruction(forge::machine::Opcode::copy_f64, 5, {4}),
            make_float_instruction(forge::machine::Opcode::return_i64, 0, {2}),
        };
        copy_chain.blocks.push_back(std::move(copy_entry));
        auto optimized_copy_chain = copy_chain;
        const auto machine_cleanup = forge::machine::optimize_function(optimized_copy_chain);
        require(machine_cleanup.copies_propagated == 4,
                "machine cleanup did not propagate the complete integer and floating copy chains");
        require(machine_cleanup.instructions_eliminated() == 5,
                "machine cleanup reported an incorrect eliminated-instruction count");
        require(machine_cleanup.dead_instructions_eliminated == 1,
                "global machine DCE did not remove the unused floating definition chain");
        require(optimized_copy_chain.register_count == 1,
                "machine cleanup did not compact dead and aliased virtual registers");
        require(optimized_copy_chain.blocks.front().instructions.size() == 2,
                "machine cleanup retained redundant or dead instructions");
        require(optimized_copy_chain.blocks.front().instructions.back().inputs.front() == 0,
                "machine cleanup did not rewrite the return through the copy chain");
        require(forge::machine::verify_module(forge::machine::Module{"copy_cleanup", {}, {optimized_copy_chain}}).empty(),
                "machine cleanup produced invalid machine IR");

        forge::machine::Function unsigned_power_of_two;
        unsigned_power_of_two.name = "unsigned_power_of_two";
        unsigned_power_of_two.register_count = 5;
        unsigned_power_of_two.register_widths.assign(5, 8);
        unsigned_power_of_two.register_classes.assign(5, forge::machine::RegisterClass::integer);
        forge::machine::Block unsigned_power_entry;
        unsigned_power_entry.name = "entry";
        unsigned_power_entry.instructions = {
            make_float_instruction(forge::machine::Opcode::load_argument_i64, 0, {}, 0),
            make_float_instruction(forge::machine::Opcode::load_immediate_i64, 1, {}, 8),
            make_float_instruction(forge::machine::Opcode::div_u_i64, 2, {0, 1}),
            make_float_instruction(forge::machine::Opcode::rem_u_i64, 3, {0, 1}),
            make_float_instruction(forge::machine::Opcode::add_i64, 4, {2, 3}),
            make_float_instruction(forge::machine::Opcode::return_i64, 0, {4}),
        };
        unsigned_power_of_two.blocks.push_back(std::move(unsigned_power_entry));
        const auto power_stats = forge::machine::optimize_function(unsigned_power_of_two);
        const auto& power_instructions = unsigned_power_of_two.blocks.front().instructions;
        require(std::none_of(power_instructions.begin(), power_instructions.end(), [](const auto& instruction) {
                    return instruction.opcode == forge::machine::Opcode::div_u_i64 ||
                           instruction.opcode == forge::machine::Opcode::rem_u_i64;
                }), "power-of-two unsigned div/rem were not strength reduced");
        require(std::any_of(power_instructions.begin(), power_instructions.end(), [](const auto& instruction) {
                    return instruction.opcode == forge::machine::Opcode::shr_u_i64 &&
                           instruction.symbol == "$imm" && instruction.immediate == 3;
                }), "unsigned division by eight did not become a shift");
        require(std::any_of(power_instructions.begin(), power_instructions.end(), [](const auto& instruction) {
                    return instruction.opcode == forge::machine::Opcode::and_i64 &&
                           instruction.symbol == "$imm" && instruction.immediate == 7;
                }), "unsigned remainder by eight did not become a mask");
        require(power_stats.immediate_forms_selected >= 2,
                "power-of-two div/rem strength reduction was not reported");
        require(forge::machine::verify_module(
                    forge::machine::Module{"unsigned_power_cleanup", {}, {unsigned_power_of_two}}).empty(),
                "power-of-two div/rem strength reduction produced invalid machine IR");
        const auto copy_allocation = forge::machine::allocate_linear_scan(copy_chain);
        require(copy_allocation.ok(), "copy-chain allocation failed");
        require(copy_allocation.coalesced_copy_count == 4,
                "linear scan did not coalesce every eligible integer and floating copy");
        require(copy_allocation.location(0).physical == copy_allocation.location(1).physical &&
                copy_allocation.location(1).physical == copy_allocation.location(2).physical,
                "integer copy chain did not retain one physical register");
        require(copy_allocation.location(3).floating == copy_allocation.location(4).floating &&
                copy_allocation.location(4).floating == copy_allocation.location(5).floating,
                "floating copy chain did not retain one XMM register");

#if defined(_WIN32)
        constexpr auto abi = forge::codegen::x86_64::Abi::windows;
#else
        constexpr auto abi = forge::codegen::x86_64::Abi::system_v;
#endif
        forge::machine::Function encoded_pressure;
        encoded_pressure.name = "encoded_pressure";
        // Keep enough simultaneously live values to exercise spill encoding
        // with the full nine-register integer pool.
        // This fixture exercises register pressure using immediates only. Do not
        // declare unused ABI arguments: on Windows an unconsumed third argument
        // lives in r8 and correctly requires an encoder capture area, which would
        // make this allocator-frame metric test ABI-dependent.
        encoded_pressure.register_count = 19;
        encoded_pressure.register_widths.assign(19, 8);
        encoded_pressure.register_classes.assign(19, forge::machine::RegisterClass::integer);
        forge::machine::Block encoded_pressure_entry;
        encoded_pressure_entry.name = "entry";
        for (forge::machine::VirtualRegister reg = 0; reg < 10; ++reg)
            encoded_pressure_entry.instructions.push_back(
                make_float_instruction(forge::machine::Opcode::load_immediate_i64, reg, {}, reg + 1));
        encoded_pressure_entry.instructions.push_back(
            make_float_instruction(forge::machine::Opcode::add_i64, 10, {0, 1}));
        for (forge::machine::VirtualRegister reg = 11; reg < 19; ++reg)
            encoded_pressure_entry.instructions.push_back(
                make_float_instruction(forge::machine::Opcode::add_i64, reg, {reg - 1, reg - 9}));
        encoded_pressure_entry.instructions.push_back(
            make_float_instruction(forge::machine::Opcode::return_i64, 0, {18}));
        encoded_pressure.blocks.push_back(std::move(encoded_pressure_entry));
        const auto encoded_pressure_allocation = forge::machine::allocate_linear_scan(encoded_pressure);
        require(encoded_pressure_allocation.ok(), "encoded pressure allocation failed");
        require(encoded_pressure_allocation.spill_count > 0,
                "encoded pressure fixture did not create spill pressure");
        require(encoded_pressure_allocation.two_address_reuse_count > 0,
                "linear scan did not reuse dying arithmetic operands");
        forge::machine::Function commutative_reuse;
        commutative_reuse.name = "commutative_reuse";
        commutative_reuse.register_count = 4;
        commutative_reuse.register_widths.assign(4, 8);
        commutative_reuse.register_classes.assign(4, forge::machine::RegisterClass::integer);
        forge::machine::Block commutative_entry;
        commutative_entry.name = "entry";
        commutative_entry.instructions.push_back(
            make_float_instruction(forge::machine::Opcode::load_immediate_i64, 0, {}, 7));
        commutative_entry.instructions.push_back(
            make_float_instruction(forge::machine::Opcode::load_immediate_i64, 1, {}, 5));
        commutative_entry.instructions.push_back(
            make_float_instruction(forge::machine::Opcode::add_i64, 2, {0, 1}));
        commutative_entry.instructions.push_back(
            make_float_instruction(forge::machine::Opcode::add_i64, 3, {2, 0}));
        commutative_entry.instructions.push_back(
            make_float_instruction(forge::machine::Opcode::return_i64, 0, {3}));
        commutative_reuse.blocks.push_back(std::move(commutative_entry));
        const auto commutative_allocation = forge::machine::allocate_linear_scan(commutative_reuse);
        require(commutative_allocation.ok(), "commutative reuse allocation failed");
        require(commutative_allocation.location(2).kind == forge::machine::LocationKind::physical_register &&
                commutative_allocation.location(2).physical == commutative_allocation.location(1).physical,
                "commutative arithmetic did not reuse the dying right operand");
        require(commutative_allocation.location(2).physical != commutative_allocation.location(0).physical,
                "commutative arithmetic unexpectedly reused the still-live left operand");
        forge::machine::Module commutative_module;
        commutative_module.name = "commutative_reuse";
        commutative_module.functions.push_back(commutative_reuse);
        const auto commutative_encoded = forge::codegen::x86_64::encode(commutative_module, abi);
        require(commutative_encoded.ok(), "commutative reuse encoding failed");
        require(commutative_encoded.functions.front().two_address_reuse_count >= 2,
                "encoder did not emit commutative right-operand reuse");

        forge::machine::Function unary_reuse;
        unary_reuse.name = "unary_reuse";
        unary_reuse.register_count = 4;
        unary_reuse.register_widths = {8, 8, 8, 8};
        unary_reuse.register_classes = {
            forge::machine::RegisterClass::integer, forge::machine::RegisterClass::integer,
            forge::machine::RegisterClass::floating, forge::machine::RegisterClass::floating};
        forge::machine::Block unary_entry;
        unary_entry.name = "entry";
        unary_entry.instructions.push_back(
            make_float_instruction(forge::machine::Opcode::load_immediate_i64, 0, {}, 9));
        unary_entry.instructions.push_back(
            make_float_instruction(forge::machine::Opcode::neg_i64, 1, {0}));
        unary_entry.instructions.push_back(
            make_float_instruction(forge::machine::Opcode::load_immediate_f64, 2, {}, 0x400c000000000000LL));
        unary_entry.instructions.push_back(
            make_float_instruction(forge::machine::Opcode::neg_f64, 3, {2}));
        unary_entry.instructions.push_back(
            make_float_instruction(forge::machine::Opcode::return_i64, 0, {1}));
        unary_reuse.blocks.push_back(std::move(unary_entry));
        const auto unary_allocation = forge::machine::allocate_linear_scan(unary_reuse);
        require(unary_allocation.ok(), "unary reuse allocation failed");
        require(unary_allocation.unary_reuse_count >= 2,
                "linear scan did not reuse dying unary operands");
        require(unary_allocation.location(1).kind == forge::machine::LocationKind::physical_register &&
                unary_allocation.location(1).physical == unary_allocation.location(0).physical,
                "integer unary result did not inherit its input register");
        require(unary_allocation.location(3).kind == forge::machine::LocationKind::floating_register &&
                unary_allocation.location(3).floating == unary_allocation.location(2).floating,
                "floating unary result did not inherit its input XMM register");
        forge::machine::Module unary_module;
        unary_module.name = "unary_reuse";
        unary_module.functions.push_back(unary_reuse);
        const auto unary_encoded = forge::codegen::x86_64::encode(unary_module, abi);
        require(unary_encoded.ok(), "unary reuse encoding failed");
        require(unary_encoded.functions.front().unary_reuse_count >= 2,
                "encoder did not emit in-place unary operations");

        forge::machine::Function slot_reuse;
        slot_reuse.name = "spill_slot_reuse";
        slot_reuse.argument_count = 12;
        slot_reuse.argument_widths.assign(slot_reuse.argument_count, 8);
        slot_reuse.argument_classes.assign(slot_reuse.argument_count, forge::machine::RegisterClass::integer);
        slot_reuse.register_count = 22;
        slot_reuse.register_widths.assign(slot_reuse.register_count, 8);
        slot_reuse.register_classes.assign(slot_reuse.register_count, forge::machine::RegisterClass::integer);
        forge::machine::Block slot_reuse_entry;
        slot_reuse_entry.name = "entry";
        for (forge::machine::VirtualRegister reg = 0; reg < 6; ++reg) {
            auto load = make_float_instruction(forge::machine::Opcode::load_argument_i64, reg, {});
            load.argument_index = reg;
            slot_reuse_entry.instructions.push_back(std::move(load));
        }
        slot_reuse_entry.instructions.push_back(
            make_float_instruction(forge::machine::Opcode::add_i64, 6, {0, 1}));
        slot_reuse_entry.instructions.push_back(
            make_float_instruction(forge::machine::Opcode::add_i64, 7, {2, 3}));
        slot_reuse_entry.instructions.push_back(
            make_float_instruction(forge::machine::Opcode::add_i64, 8, {4, 5}));
        slot_reuse_entry.instructions.push_back(
            make_float_instruction(forge::machine::Opcode::add_i64, 9, {6, 7}));
        slot_reuse_entry.instructions.push_back(
            make_float_instruction(forge::machine::Opcode::add_i64, 10, {9, 8}));
        for (forge::machine::VirtualRegister reg = 11; reg < 17; ++reg) {
            auto load = make_float_instruction(forge::machine::Opcode::load_argument_i64, reg, {});
            load.argument_index = reg - 5;
            slot_reuse_entry.instructions.push_back(std::move(load));
        }
        slot_reuse_entry.instructions.push_back(
            make_float_instruction(forge::machine::Opcode::add_i64, 17, {11, 12}));
        slot_reuse_entry.instructions.push_back(
            make_float_instruction(forge::machine::Opcode::add_i64, 18, {13, 14}));
        slot_reuse_entry.instructions.push_back(
            make_float_instruction(forge::machine::Opcode::add_i64, 19, {15, 16}));
        slot_reuse_entry.instructions.push_back(
            make_float_instruction(forge::machine::Opcode::add_i64, 20, {17, 18}));
        slot_reuse_entry.instructions.push_back(
            make_float_instruction(forge::machine::Opcode::add_i64, 21, {20, 19}));
        slot_reuse_entry.instructions.push_back(
            make_float_instruction(forge::machine::Opcode::return_i64, 0, {21}));
        slot_reuse.blocks.push_back(std::move(slot_reuse_entry));
        const auto slot_reuse_allocation = forge::machine::allocate_linear_scan(slot_reuse);
        require(slot_reuse_allocation.ok(), "spill-slot reuse allocation failed");
        require(slot_reuse_allocation.spill_count <= 1 ||
                slot_reuse_allocation.spill_count > slot_reuse_allocation.spill_slot_count,
                "non-overlapping spilled values did not share physical frame slots");
        require(slot_reuse_allocation.spill_count <= 1 ||
                slot_reuse_allocation.reused_spill_slot_count > 0,
                "spill-slot allocator did not report reused slots");
        require(slot_reuse_allocation.frame_size <= slot_reuse_allocation.frame_size_before_slot_reuse,
                "spill-slot reuse increased frame size");

        forge::machine::Module weighted_module;
        weighted_module.name = "weighted_metrics";
        weighted_module.functions.push_back(encoded_pressure);
        const auto weighted_encoded = forge::codegen::x86_64::encode(weighted_module, abi);
        require(weighted_encoded.ok(), "weighted metrics encoding failed");
        const auto& weighted_stats = weighted_encoded.functions.front();
        require(weighted_stats.encoded_byte_count == weighted_stats.code.size(),
                "encoded byte metric did not match emitted code size");
        require(weighted_stats.frame_size == encoded_pressure_allocation.frame_size,
                "encoded frame metric did not match register allocation");
        require(weighted_stats.spill_slot_count == encoded_pressure_allocation.spill_slot_count,
                "encoded physical spill-slot metric did not match register allocation");
        require(weighted_stats.spilled_value_count == encoded_pressure_allocation.spill_count,
                "encoded spilled-value metric did not match register allocation");
        require(weighted_stats.rematerialized_value_count > 0,
                "spill pressure fixture did not report rematerialized constants");
        require(weighted_stats.rematerialized_definition_count > 0,
                "encoder did not eliminate rematerialized constant definitions");
        require(weighted_stats.spill_slot_count == 0,
                "rematerialized pressure fixture retained physical spill slots");
        require(weighted_stats.pre_optimization_encoded_byte_count >= weighted_stats.encoded_byte_count,
                "pre-optimization byte metric was smaller than final code");
        require(weighted_stats.pre_optimization_encoded_byte_count - weighted_stats.encoded_byte_count ==
                    weighted_stats.eliminated_encoded_byte_count,
                "eliminated-byte accounting was not exact");
        require(weighted_stats.two_address_reuse_count > 0,
                "encoder did not emit a two-address arithmetic reuse");

        forge::machine::Function duplicate_spill;
        duplicate_spill.name = "duplicate_spill_reload";
        duplicate_spill.register_count = 16;
        duplicate_spill.register_widths.assign(16, 8);
        duplicate_spill.register_classes.assign(16, forge::machine::RegisterClass::integer);
        forge::machine::Block duplicate_entry;
        duplicate_entry.name = "entry";
        for (forge::machine::VirtualRegister reg = 0; reg < 6; ++reg)
            duplicate_entry.instructions.push_back(
                make_float_instruction(forge::machine::Opcode::load_immediate_i64, reg, {}, reg + 1));
        forge::machine::VirtualRegister next = 6;
        for (int repeat = 0; repeat < 4; ++repeat) {
            duplicate_entry.instructions.push_back(
                make_float_instruction(forge::machine::Opcode::add_i64, next++, {0, 1}));
            duplicate_entry.instructions.push_back(
                make_float_instruction(forge::machine::Opcode::add_i64, next++, {2, 3}));
        }
        const auto duplicate_result = next++;
        duplicate_entry.instructions.push_back(
            make_float_instruction(forge::machine::Opcode::add_i64, duplicate_result, {4, 4}));
        const auto final_result = next++;
        duplicate_entry.instructions.push_back(
            make_float_instruction(forge::machine::Opcode::add_i64, final_result, {duplicate_result, 5}));
        duplicate_entry.instructions.push_back(
            make_float_instruction(forge::machine::Opcode::return_i64, 0, {final_result}));
        duplicate_spill.register_count = next;
        duplicate_spill.register_widths.resize(next, 8);
        duplicate_spill.register_classes.resize(next, forge::machine::RegisterClass::integer);
        duplicate_spill.blocks.push_back(std::move(duplicate_entry));
        const auto duplicate_allocation = forge::machine::allocate_linear_scan(duplicate_spill);
        require(duplicate_allocation.ok(), "duplicate spill reload allocation failed");
        const bool duplicated_operand_spilled =
            duplicate_allocation.location(4).kind == forge::machine::LocationKind::stack_slot;
        forge::machine::Module duplicate_module;
        duplicate_module.name = "duplicate_spill_reload";
        duplicate_module.functions.push_back(duplicate_spill);
        const auto duplicate_encoded = forge::codegen::x86_64::encode(duplicate_module, abi);
        require(duplicate_encoded.ok(), "duplicate spill reload encoding failed");
        if (duplicated_operand_spilled) {
            require(duplicate_encoded.functions.front().redundant_spill_load_count > 0,
                    "encoder did not eliminate a duplicate reload of the same spilled operand");
            require(duplicate_encoded.functions.front().cached_spill_load_count > 0,
                    "encoder did not reuse a cached spill value across adjacent arithmetic instructions");
            require(duplicate_encoded.functions.front().spill_store_cache_count > 0,
                    "encoder did not prime the spill cache after stack-backed result stores");
            require(duplicate_encoded.functions.front().deferred_spill_store_count > 0,
                    "encoder did not retain stack-backed results as deferred stores");
            require(duplicate_encoded.functions.front().spill_cache_invalidation_count > 0,
                    "encoder did not report edge/return cache invalidation");
            require(duplicate_encoded.functions.front().spill_cache_hit_count > 0,
                    "encoder did not report multi-entry spill-cache hits");
            require(duplicate_encoded.functions.front().spill_cache_peak_resident_count >= 2,
                    "encoder did not keep multiple spill slots resident simultaneously");
            require(duplicate_encoded.functions.front().avoided_spill_load_byte_count > 0,
                    "encoder did not report exact avoided spill-load bytes");
            require(duplicate_encoded.functions.front().spill_cache_preserved_instruction_count > 0,
                    "encoder did not preserve spill-cache state across safe straight-line instructions");
            require(duplicate_encoded.functions.front().spill_cache_boundary_flush_count > 0,
                    "encoder did not report spill-cache boundary flushes");
        }

        require(weighted_stats.spill_cache_last_use_drop_count > 0 ||
                weighted_stats.rematerialized_definition_count > 0,
                "encoder neither dropped a pending final-use store nor rematerialized it away");

        forge::machine::Function dead_float_spill;
        dead_float_spill.name = "dead_float_spill";
        dead_float_spill.argument_count = 1;
        dead_float_spill.argument_widths = {8};
        dead_float_spill.argument_classes = {forge::machine::RegisterClass::floating};
        dead_float_spill.register_count = 1;
        dead_float_spill.register_widths = {8};
        dead_float_spill.register_classes = {forge::machine::RegisterClass::floating};
        forge::machine::Block dead_float_entry;
        dead_float_entry.name = "entry";
        auto dead_load = make_float_instruction(forge::machine::Opcode::load_argument_f64, 0, {});
        dead_load.argument_index = 0;
        dead_float_entry.instructions.push_back(std::move(dead_load));
        dead_float_entry.instructions.push_back(
            make_float_instruction(forge::machine::Opcode::return_void, 0, {}));
        dead_float_spill.blocks.push_back(std::move(dead_float_entry));
        forge::machine::Module dead_float_module;
        dead_float_module.name = "dead_float_spill";
        dead_float_module.functions.push_back(std::move(dead_float_spill));
        const auto dead_float_encoded = forge::codegen::x86_64::encode(dead_float_module, abi);
        require(dead_float_encoded.ok(), "dead floating spill encoding failed");
        const auto& dead_float_stats = dead_float_encoded.functions.front();
        require(dead_float_stats.spilled_value_count == 0 && dead_float_stats.spill_store_count == 0,
                "unused floating entry argument was unnecessarily forced through a spill slot");
        require(dead_float_stats.dead_spill_store_count == 0,
                "register-resident floating entry argument reported a synthetic dead spill store");


        forge::machine::Module copy_module;
        copy_module.name = "copy_coalescing";
        copy_module.functions.push_back(copy_chain);
        const auto copy_encoded = forge::codegen::x86_64::encode(copy_module, abi);
        require(copy_encoded.ok(), "coalesced copy-chain encoding failed");
        require(copy_encoded.functions.front().eliminated_copy_count == 4,
                "encoder did not eliminate coalesced integer and floating copies");
        require(copy_encoded.functions.front().emitted_copy_count == 0,
                "encoder emitted a redundant copy after coalescing");
        require(copy_encoded.functions.front().machine_instruction_count == 7,
                "machine instruction statistics did not count the copy-chain fixture");

        auto encoded = forge::codegen::x86_64::encode(*lowered.module, abi);

        constexpr auto call_source = R"(module @calls {
extern func @host_add(%left: i32, %right: i32) -> i32
extern func @host_record(%value: i32) -> void
func @factorial(%value: i32) -> i32 {
entry:
  %one = const i32 1
  %base = cmp.le i32 %value %one
  branch %base, done(%one), recurse(%value)
recurse(%current: i32):
  %next = sub i32 %current %one
  %partial = call i32 @factorial(%next)
  %product = mul i32 %current %partial
  return %product
done(%base_result: i32):
  return %base_result
}
func @add_two(%left: i32, %right: i32) -> i32 {
entry:
  %sum = add i32 %left %right
  return %sum
}
func @call_add(%left: i32, %right: i32) -> i32 {
entry:
  %sum = call i32 @add_two(%left, %right)
  return %sum
}
func @sum_eight(%a: i32, %b: i32, %c: i32, %d: i32, %e: i32, %f: i32, %g: i32, %h: i32) -> i32 {
entry:
  %ab = add i32 %a %b
  %cd = add i32 %c %d
  %ef = add i32 %e %f
  %gh = add i32 %g %h
  %abcd = add i32 %ab %cd
  %efgh = add i32 %ef %gh
  %all = add i32 %abcd %efgh
  return %all
}
func @call_host(%left: i32, %right: i32) -> i32 {
entry:
  %sum = call i32 @host_add(%left, %right)
  return %sum
}
func @call_record(%value: i32) -> void {
entry:
  call void @host_record(%value)
  return
}
func @increment(%value: i32) -> i32 {
entry:
  %one = const i32 1
  %result = add i32 %value %one
  return %result
}
func @call_pointer(%value: i32) -> i32 {
entry:
  %target = func.address ptr @increment
  %result = call.indirect i32 %target as @increment(%value)
  return %result
}
func @record_internal(%value: i32) -> void {
entry:
  call void @host_record(%value)
  return
}
func @call_void_pointer(%value: i32) -> void {
entry:
  %target = func.address ptr @record_internal
  call.indirect void %target as @record_internal(%value)
  return
}
func @call_sum_eight() -> i32 {
entry:
  %a = const i32 1
  %b = const i32 2
  %c = const i32 3
  %d = const i32 4
  %e = const i32 5
  %f = const i32 6
  %g = const i32 7
  %h = const i32 8
  %sum = call i32 @sum_eight(%a, %b, %c, %d, %e, %f, %g, %h)
  return %sum
}
})";
        auto call_parsed = forge::ir::parse_module(call_source);
        require(call_parsed.ok(), "call fixture did not parse");
        require(forge::ir::verify_module(*call_parsed.module).empty(), "call fixture did not verify");
        auto call_lowered = forge::machine::lower_module(*call_parsed.module);
        require(call_lowered.ok(), "call fixture did not lower");
        require(forge::machine::print_module(*call_lowered.module).find("call_i32 @factorial") != std::string::npos,
                "machine IR lost recursive call");
        auto call_image = forge::codegen::x86_64::encode_image(*call_lowered.module, abi);
        require(call_image.ok(), "internal-call module image encoding failed");
        require(call_image.image.entries.size() == 11, "call image lost function entries");
        require(call_image.image.externals.size() == 2, "external relocations were not emitted");
        auto relocation_diagnostics = forge::codegen::x86_64::resolve_externals(call_image.image, [](std::string_view symbol) -> std::optional<std::uintptr_t> {
            if (symbol == "host_add") return reinterpret_cast<std::uintptr_t>(&forge_test_host_add);
            if (symbol == "host_record") return reinterpret_cast<std::uintptr_t>(&forge_test_host_record);
            return std::nullopt;
        });
        require(relocation_diagnostics.empty(), "external symbol resolution failed");
        auto unresolved_copy = forge::codegen::x86_64::encode_image(*call_lowered.module, abi);
        auto unresolved_diagnostics = forge::codegen::x86_64::resolve_externals(unresolved_copy.image, [](std::string_view) -> std::optional<std::uintptr_t> { return std::nullopt; });
        require(!unresolved_diagnostics.empty(), "missing external symbol was accepted");
        auto jit = forge::jit::load(*call_lowered.module, abi, [](std::string_view symbol) -> std::optional<std::uintptr_t> {
            if (symbol == "host_add") return reinterpret_cast<std::uintptr_t>(&forge_test_host_add);
            if (symbol == "host_record") return reinterpret_cast<std::uintptr_t>(&forge_test_host_record);
            return std::nullopt;
        });
        require(jit.ok(), "JIT engine failed to load call module");
        require(jit.engine->code_size() == call_image.image.code.size(), "JIT code size mismatch");
        require(jit.engine->lookup("missing") == nullptr, "JIT lookup accepted unknown symbol");

        constexpr auto global_source = R"(module @native_globals {
global @counter: i32 = 40
constant @increment: i32 = 2
func @update_counter() -> i32 {
entry:
  %counter = global.address ptr @counter
  %increment = global.address ptr @increment
  %left = load i32 %counter
  %right = load i32 %increment
  %sum = add i32 %left %right
  store i32 %sum %counter
  return %sum
}
func @read_counter() -> i32 {
entry:
  %counter = global.address ptr @counter
  %value = load i32 %counter
  return %value
}
})";
        auto global_parsed = forge::ir::parse_module(global_source);
        require(global_parsed.ok(), "native global fixture did not parse");
        require(forge::ir::verify_module(*global_parsed.module).empty(), "native global fixture did not verify");
        auto global_lowered = forge::machine::lower_module(*global_parsed.module);
        require(global_lowered.ok(), "native global fixture did not lower");
        require(global_lowered.module->globals.size() == 2, "machine globals were lost");
        require(forge::machine::print_module(*global_lowered.module).find("load_global_address") != std::string::npos, "machine IR lost global address");
        auto global_jit = forge::jit::load(*global_lowered.module, abi);
        require(global_jit.ok(), "JIT failed to load native globals");
        require(global_jit.engine->lookup_global("counter") != nullptr, "JIT global lookup failed");
#if defined(__unix__) && (defined(__x86_64__) || defined(__amd64__))
        {
            using NoArgs = int (*)();
            auto update = reinterpret_cast<NoArgs>(global_jit.engine->lookup("update_counter"));
            auto read = reinterpret_cast<NoArgs>(global_jit.engine->lookup("read_counter"));
            require(update && read, "native global entries missing");
            require(update() == 42, "native global update returned wrong result");
            require(read() == 42, "native global state did not persist");
            require(*static_cast<int*>(global_jit.engine->lookup_global("counter")) == 42, "native global lookup exposed wrong value");
        }
#endif


        constexpr auto wide_source = R"(module @native_i64 {
global @wide_counter: i64 = 5000000000
constant @wide_increment: i64 = 7
func @wide_math(%left: i64, %right: i64) -> i64 {
entry:
  %sum = add i64 %left %right
  %scale = const i64 3
  %result = mul i64 %sum %scale
  return %result
}
func @update_wide() -> i64 {
entry:
  %counter = global.address ptr @wide_counter
  %increment = global.address ptr @wide_increment
  %left = load i64 %counter
  %right = load i64 %increment
  %sum = add i64 %left %right
  store i64 %sum %counter
  return %sum
}
})";
        auto wide_parsed = forge::ir::parse_module(wide_source);
        require(wide_parsed.ok(), "native i64 fixture did not parse");
        require(forge::ir::verify_module(*wide_parsed.module).empty(), "native i64 fixture did not verify");
        auto wide_lowered = forge::machine::lower_module(*wide_parsed.module);
        require(wide_lowered.ok(), "native i64 fixture did not lower");
        require(wide_lowered.module->globals.size() == 2 && wide_lowered.module->globals[0].size == 8,
                "native i64 global width was lost");
        auto wide_jit = forge::jit::load(*wide_lowered.module, abi);
        require(wide_jit.ok(), "JIT failed to load native i64 module");
#if defined(__unix__) && (defined(__x86_64__) || defined(__amd64__))
        {
            using WideMath = std::int64_t (*)(std::int64_t, std::int64_t);
            using WideNoArgs = std::int64_t (*)();
            auto wide_math = reinterpret_cast<WideMath>(wide_jit.engine->lookup("wide_math"));
            auto update_wide = reinterpret_cast<WideNoArgs>(wide_jit.engine->lookup("update_wide"));
            require(wide_math && update_wide, "native i64 entries missing");
            require(wide_math(4'000'000'000LL, 2'000'000'000LL) == 18'000'000'000LL,
                    "native i64 arithmetic returned wrong result");
            require(update_wide() == 5'000'000'007LL, "native i64 global update returned wrong result");
            require(*static_cast<std::int64_t*>(wide_jit.engine->lookup_global("wide_counter")) == 5'000'000'007LL,
                    "native i64 global state did not persist");
        }
#endif

        constexpr auto wide_control_source = R"(module @wide_control {
func @sum_to_wide(%limit: i64) -> i64 {
entry:
  %zero = const i64 0
  %one = const i64 1
  jump loop(%zero, %zero)
loop(%index: i64, %total: i64):
  %done = cmp.ge i64 %index %limit
  branch %done, exit(%total), body(%index, %total)
body(%current: i64, %running: i64):
  %next_total = add i64 %running %current
  %next_index = add i64 %current %one
  jump loop(%next_index, %next_total)
exit(%result: i64):
  return %result
}
func @fib_wide(%n: i64) -> i64 {
entry:
  %zero = const i64 0
  %one = const i64 1
  jump loop(%zero, %one, %n)
loop(%a: i64, %b: i64, %left: i64):
  %done = cmp.eq i64 %left %zero
  branch %done, exit(%a), body(%a, %b, %left)
body(%current_a: i64, %current_b: i64, %current_left: i64):
  %next = add i64 %current_a %current_b
  %next_left = sub i64 %current_left %one
  jump loop(%current_b, %next, %next_left)
exit(%result: i64):
  return %result
}
func @add_wide(%left: i64, %right: i64) -> i64 {
entry:
  %sum = add i64 %left %right
  return %sum
}
func @call_wide(%left: i64, %right: i64) -> i64 {
entry:
  %sum = call i64 @add_wide(%left, %right)
  return %sum
}
func @stack_wide(%value: i64) -> i64 {
entry:
  %slot = stack.alloc ptr 8
  store i64 %value %slot
  %loaded = load i64 %slot
  return %loaded
}
})";
        auto wide_control_parsed = forge::ir::parse_module(wide_control_source);
        require(wide_control_parsed.ok(), "wide control fixture did not parse");
        require(forge::ir::verify_module(*wide_control_parsed.module).empty(), "wide control fixture did not verify");
        auto wide_control_lowered = forge::machine::lower_module(*wide_control_parsed.module);
        require(wide_control_lowered.ok(), "wide control fixture did not lower");
        require(forge::machine::print_module(*wide_control_lowered.module).find("cmp_ge_i64") != std::string::npos,
                "wide comparison was not preserved in machine IR");
        const auto wide_loop_allocation = forge::machine::allocate_linear_scan(
            wide_control_lowered.module->functions.front());
        require(wide_loop_allocation.ok(), "wide loop register allocation failed");
        require(wide_loop_allocation.location(3).kind == forge::machine::LocationKind::physical_register &&
                wide_loop_allocation.location(8).kind == forge::machine::LocationKind::physical_register,
                "wide induction values were not register allocated");
        require(wide_loop_allocation.two_address_reuse_count != 0,
                "immediate induction update did not participate in two-address reuse");
        require(wide_loop_allocation.location(3).kind == forge::machine::LocationKind::physical_register &&
                wide_loop_allocation.location(8).kind == forge::machine::LocationKind::physical_register &&
                wide_loop_allocation.location(1).kind == forge::machine::LocationKind::physical_register &&
                wide_loop_allocation.location(3).physical == wide_loop_allocation.location(8).physical &&
                wide_loop_allocation.location(8).physical == wide_loop_allocation.location(1).physical,
                "induction update was not destructively coalesced across the loop backedge");
        const auto stack_wide_function = std::find_if(wide_control_lowered.module->functions.begin(),
            wide_control_lowered.module->functions.end(), [](const auto& function) { return function.name == "stack_wide"; });
        require(stack_wide_function != wide_control_lowered.module->functions.end(), "wide fixed-stack function missing");
        require(stack_wide_function->machine_load_returns_folded == 1U,
                "wide fixed-stack load was not folded into the return");
        auto wide_control_jit = forge::jit::load(*wide_control_lowered.module, abi);
        require(wide_control_jit.ok(), "JIT failed to load wide control module");
#if defined(__unix__) && (defined(__x86_64__) || defined(__amd64__))
        {
            using One = std::int64_t (*)(std::int64_t);
            using Two = std::int64_t (*)(std::int64_t, std::int64_t);
            auto sum_to_wide = reinterpret_cast<One>(wide_control_jit.engine->lookup("sum_to_wide"));
            auto fib_wide = reinterpret_cast<One>(wide_control_jit.engine->lookup("fib_wide"));
            auto call_wide = reinterpret_cast<Two>(wide_control_jit.engine->lookup("call_wide"));
            auto stack_wide = reinterpret_cast<One>(wide_control_jit.engine->lookup("stack_wide"));
            require(sum_to_wide && fib_wide && call_wide && stack_wide, "wide control entries missing");
            require(sum_to_wide(100'000) == 4'999'950'000LL, "i64 loop/block arguments returned wrong result");
            require(fib_wide(20) == 6765, "destructive induction coalescing corrupted an unrelated live constant");
            require(call_wide(5'000'000'000LL, 7) == 5'000'000'007LL, "i64 direct call returned wrong result");
            require(stack_wide(9'000'000'000LL) == 9'000'000'000LL, "i64 fixed-stack load/store returned wrong result");
        }
#endif

        constexpr auto wide_integer_ops_source = R"(module @wide_integer_ops {
func @wide_signed(%left: i64, %right: i64) -> i64 {
entry:
  %quotient = div.signed i64 %left %right
  %remainder = rem.signed i64 %left %right
  %three = const i64 3
  %shifted = shl i64 %quotient %three
  %mixed = xor i64 %shifted %remainder
  %inverted = not i64 %mixed
  %result = neg i64 %inverted
  return %result
}
func @wide_unsigned(%left: i64, %right: i64) -> i64 {
entry:
  %quotient = div.unsigned i64 %left %right
  %remainder = rem.unsigned i64 %left %right
  %one = const i64 1
  %shifted = shr.unsigned i64 %quotient %one
  %masked = and i64 %shifted %left
  %result = or i64 %masked %remainder
  return %result
}
func @wide_arithmetic_shift(%value: i64) -> i64 {
entry:
  %two = const i64 2
  %result = shr.signed i64 %value %two
  return %result
}
})";
        auto wide_integer_ops_parsed = forge::ir::parse_module(wide_integer_ops_source);
        require(wide_integer_ops_parsed.ok(), "wide integer operation fixture did not parse");
        require(forge::ir::verify_module(*wide_integer_ops_parsed.module).empty(), "wide integer operation fixture did not verify");
        auto wide_integer_ops_lowered = forge::machine::lower_module(*wide_integer_ops_parsed.module);
        require(wide_integer_ops_lowered.ok(), "wide integer operation fixture did not lower");
        const auto wide_integer_machine = forge::machine::print_module(*wide_integer_ops_lowered.module);
        require(wide_integer_machine.find("div_s_i64") != std::string::npos, "signed i64 division was not lowered");
        require(wide_integer_machine.find("rem_u_i64") != std::string::npos, "unsigned i64 remainder was not lowered");
        require(wide_integer_machine.find("shr_s_i64") != std::string::npos, "signed i64 shift was not lowered");
        auto wide_integer_ops_jit = forge::jit::load(*wide_integer_ops_lowered.module, abi);
        require(wide_integer_ops_jit.ok(), "JIT failed to load wide integer operation module");
#if defined(__unix__) && (defined(__x86_64__) || defined(__amd64__))
        {
            using Binary = std::int64_t (*)(std::int64_t, std::int64_t);
            using Unary = std::int64_t (*)(std::int64_t);
            auto wide_signed = reinterpret_cast<Binary>(wide_integer_ops_jit.engine->lookup("wide_signed"));
            auto wide_unsigned = reinterpret_cast<Binary>(wide_integer_ops_jit.engine->lookup("wide_unsigned"));
            auto wide_arithmetic_shift = reinterpret_cast<Unary>(wide_integer_ops_jit.engine->lookup("wide_arithmetic_shift"));
            require(wide_signed && wide_unsigned && wide_arithmetic_shift, "wide integer operation entries missing");
            const std::int64_t signed_left = -9'000'000'011LL;
            const std::int64_t signed_right = 97;
            const auto signed_q = signed_left / signed_right;
            const auto signed_r = signed_left % signed_right;
            const auto signed_expected = -~((signed_q << 3) ^ signed_r);
            require(wide_signed(signed_left, signed_right) == signed_expected, "signed i64 operation family returned wrong result");
            const std::uint64_t unsigned_left = 0xF123'4567'89AB'CDEFULL;
            const std::uint64_t unsigned_right = 257;
            const auto unsigned_q = unsigned_left / unsigned_right;
            const auto unsigned_r = unsigned_left % unsigned_right;
            const auto unsigned_expected = ((unsigned_q >> 1) & unsigned_left) | unsigned_r;
            require(static_cast<std::uint64_t>(wide_unsigned(static_cast<std::int64_t>(unsigned_left), static_cast<std::int64_t>(unsigned_right))) == unsigned_expected,
                    "unsigned i64 operation family returned wrong result");
            require(wide_arithmetic_shift(-128) == -32, "signed i64 right shift returned wrong result");
        }
#endif

        require(encoded.ok(), "x86-64 encoding failed");
        require(encoded.functions.size() == 7, "not all functions emitted machine code");
        for (const auto& function : encoded.functions)
            require(!function.code.empty(), "empty native function emitted");

#if defined(__unix__) && (defined(__x86_64__) || defined(__amd64__))
        {
            using OneArg = int (*)(int);
            auto function = reinterpret_cast<OneArg>(jit.engine->lookup("call_pointer"));
            require(function != nullptr && function(41) == 42, "JIT lookup or indirect call failed");
        }
        {
            void* memory = nullptr;
            std::size_t size = 0;
            using Function = int (*)(int, int);
            auto function = make_executable<Function>(encoded.functions[0].code, memory, size);
            require(function(7, 5) == 36, "straight-line native function returned wrong result");
            ::munmap(memory, size);
        }
        {
            void* memory = nullptr;
            std::size_t size = 0;
            using Function = int (*)(int, int);
            auto function = make_executable<Function>(encoded.functions[1].code, memory, size);
            require(function(7, 5) == 7, "true branch returned wrong result");
            require(function(-3, 9) == 9, "false branch returned wrong result");
            ::munmap(memory, size);
        }
        {
            void* memory = nullptr;
            std::size_t size = 0;
            using Function = int (*)(int);
            auto function = make_executable<Function>(encoded.functions[2].code, memory, size);
            require(function(0) == 0, "zero-trip loop returned wrong result");
            require(function(5) == 10, "native loop returned wrong result");
            require(function(10) == 45, "native backedge returned wrong result");
            ::munmap(memory, size);
        }
        {
            void* memory = nullptr;
            std::size_t size = 0;
            using Function = int (*)(int, int);
            auto function = make_executable<Function>(encoded.functions[3].code, memory, size);
            require(function(20, 6) == 4, "integer division/shift/xor returned wrong result");
            require(function(-20, 6) == 4, "signed division/remainder returned wrong result");
            ::munmap(memory, size);
        }
        {
            void* memory = nullptr;
            std::size_t size = 0;
            using Function = int (*)(int, int);
            auto function = make_executable<Function>(encoded.functions[4].code, memory, size);
            require(function(-1, 1) == -1, "unsigned comparison returned wrong result");
            require(function(1, 2) == 2, "unsigned comparison false edge returned wrong result");
            ::munmap(memory, size);
        }
        {
            void* memory = nullptr;
            std::size_t size = 0;
            using Function = int (*)(int, int);
            auto function = make_executable<Function>(encoded.functions[5].code, memory, size);
            require(function(17, 25) == 42, "stack load/store returned wrong result");
            require(function(-10, 3) == -7, "stack memory preserved signed values incorrectly");
            ::munmap(memory, size);
        }
        {
            void* memory = nullptr;
            std::size_t size = 0;
            using Function = int (*)(int, int, int, int, int, int, int, int);
            auto function = make_executable<Function>(encoded.functions[6].code, memory, size);
            require(function(1, 2, 3, 4, 5, 6, 7, 8) == 36,
                    "stack-passed entry arguments returned wrong result");
            ::munmap(memory, size);
        }
        {
            void* memory = nullptr;
            std::size_t size = 0;
            using Factorial = int (*)(int);
            auto base = make_executable<void*>(call_image.image.code, memory, size);
            (void)base;
            auto entry_offset = [&](const std::string& name) {
                for (const auto& entry : call_image.image.entries) if (entry.first == name) return entry.second;
                throw std::runtime_error("missing native image entry");
            };
            auto factorial = reinterpret_cast<Factorial>(static_cast<std::byte*>(memory) + entry_offset("factorial"));
            using Add = int (*)(int, int);
            auto add = reinterpret_cast<Add>(static_cast<std::byte*>(memory) + entry_offset("call_add"));
            using NoArgs = int (*)();
            auto sum_eight = reinterpret_cast<NoArgs>(static_cast<std::byte*>(memory) + entry_offset("call_sum_eight"));
            auto host_add = reinterpret_cast<Add>(static_cast<std::byte*>(memory) + entry_offset("call_host"));
            using VoidOne = void (*)(int);
            auto record = reinterpret_cast<VoidOne>(static_cast<std::byte*>(memory) + entry_offset("call_record"));
            using OneArg = int (*)(int);
            auto call_pointer = reinterpret_cast<OneArg>(static_cast<std::byte*>(memory) + entry_offset("call_pointer"));
            auto call_void_pointer = reinterpret_cast<VoidOne>(static_cast<std::byte*>(memory) + entry_offset("call_void_pointer"));
            require(factorial(1) == 1, "recursive base case failed");
            require(factorial(5) == 120, "recursive internal call failed");
            require(add(20, 22) == 42, "multi-argument internal call failed");
            require(host_add(19, 23) == 42, "external host call failed");
            forge_test_recorded = 0;
            record(77);
            require(forge_test_recorded == 77, "external void host call failed");
            require(sum_eight() == 36, "stack-passed internal call arguments failed");
            require(call_pointer(41) == 42, "indirect value-returning call failed");
            forge_test_recorded = 0;
            call_void_pointer(88);
            require(forge_test_recorded == 88, "indirect void call failed");
            ::munmap(memory, size);
        }
#endif
        std::cout << "Forge CFG lowering, allocation, and x86-64 codegen tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "test failure: " << error.what() << '\n';
        return 1;
    }
}
