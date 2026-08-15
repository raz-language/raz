// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/machine/liveness.hpp"

#include <unordered_map>

namespace forge::machine {
namespace {

bool has_result(Opcode opcode) noexcept {
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

void add_use(std::vector<bool>& live, VirtualRegister reg) {
    if (reg < live.size()) live[reg] = true;
}

} // namespace

LivenessAnalysis analyze_liveness(const Function& function) {
    LivenessAnalysis analysis;
    const auto block_count = function.blocks.size();
    analysis.uses.assign(block_count, std::vector<bool>(function.register_count));
    analysis.defs.assign(block_count, std::vector<bool>(function.register_count));
    analysis.live_in.assign(block_count, std::vector<bool>(function.register_count));
    analysis.live_out.assign(block_count, std::vector<bool>(function.register_count));
    analysis.successors.resize(block_count);

    std::unordered_map<std::string, std::size_t> block_indices;
    for (std::size_t index = 0; index < block_count; ++index)
        block_indices.emplace(function.blocks[index].name, index);

    for (std::size_t block_index = 0; block_index < block_count; ++block_index) {
        const auto& block = function.blocks[block_index];
        for (const auto parameter : block.parameters)
            if (parameter < function.register_count) analysis.defs[block_index][parameter] = true;
        for (const auto& instruction : block.instructions) {
            const auto record_use = [&](VirtualRegister reg) {
                if (reg < function.register_count && !analysis.defs[block_index][reg])
                    analysis.uses[block_index][reg] = true;
            };
            for (const auto input : instruction.inputs) record_use(input);
            for (const auto& successor : instruction.successors) {
                for (const auto argument : successor.arguments) record_use(argument);
                const auto target = block_indices.find(successor.block);
                if (target != block_indices.end()) analysis.successors[block_index].push_back(target->second);
            }
            if (has_result(instruction.opcode) && instruction.result < function.register_count)
                analysis.defs[block_index][instruction.result] = true;
        }
    }

    bool changed = true;
    while (changed) {
        changed = false;
        ++analysis.fixed_point_iterations;
        for (std::size_t reverse = block_count; reverse > 0; --reverse) {
            const auto block_index = reverse - 1U;
            auto next_out = std::vector<bool>(function.register_count);
            for (const auto successor : analysis.successors[block_index])
                for (VirtualRegister reg = 0; reg < function.register_count; ++reg)
                    next_out[reg] = next_out[reg] || analysis.live_in[successor][reg];

            auto next_in = analysis.uses[block_index];
            for (VirtualRegister reg = 0; reg < function.register_count; ++reg)
                next_in[reg] = next_in[reg] || (next_out[reg] && !analysis.defs[block_index][reg]);

            if (next_out != analysis.live_out[block_index] || next_in != analysis.live_in[block_index]) {
                analysis.live_out[block_index] = std::move(next_out);
                analysis.live_in[block_index] = std::move(next_in);
                changed = true;
            }
        }
    }

    analysis.live_after.resize(block_count);
    for (std::size_t block_index = 0; block_index < block_count; ++block_index) {
        const auto& block = function.blocks[block_index];
        auto live = analysis.live_out[block_index];
        analysis.live_after[block_index].resize(block.instructions.size());
        for (std::size_t reverse = block.instructions.size(); reverse > 0; --reverse) {
            const auto instruction_index = reverse - 1U;
            const auto& instruction = block.instructions[instruction_index];
            analysis.live_after[block_index][instruction_index] = live;
            if (has_result(instruction.opcode) && instruction.result < live.size())
                live[instruction.result] = false;
            for (const auto input : instruction.inputs) add_use(live, input);
            for (const auto& successor : instruction.successors)
                for (const auto argument : successor.arguments) add_use(live, argument);
        }
        for (VirtualRegister reg = 0; reg < function.register_count; ++reg)
            if (analysis.live_out[block_index][reg]) ++analysis.cross_block_live_values;
        for (const auto& instruction : block.instructions)
            for (const auto& successor : instruction.successors)
                analysis.cross_block_live_values += static_cast<std::uint32_t>(successor.arguments.size());
    }
    return analysis;
}

} // namespace forge::machine
