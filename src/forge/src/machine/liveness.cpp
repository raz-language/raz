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
    case Opcode::store_stack_v128: case Opcode::store_stack_v256: case Opcode::store_stack_v512:
    case Opcode::store_ptr_i8: case Opcode::store_ptr_i16: case Opcode::store_ptr_i32:
    case Opcode::store_ptr_i64: case Opcode::store_ptr_f32: case Opcode::store_ptr_f64:
    case Opcode::add_i64_contiguous_inplace:
    case Opcode::binary_i32_contiguous_inplace:
    case Opcode::binary_i64_contiguous_inplace:
    case Opcode::binary_i32_contiguous_map:
    case Opcode::binary_i64_contiguous_map:
    case Opcode::binary_i32_contiguous_map2:
    case Opcode::binary_i64_contiguous_map2:
    case Opcode::binary_i32_contiguous_map3:
    case Opcode::binary_i64_contiguous_map3:
    case Opcode::binary_i32_contiguous_chain:
    case Opcode::binary_i64_contiguous_chain:
    case Opcode::binary_i32_contiguous_dag:
    case Opcode::binary_i64_contiguous_dag:
    case Opcode::binary_i32_contiguous_dag_reuse:
    case Opcode::binary_i64_contiguous_dag_reuse:
    case Opcode::call_void: case Opcode::call_aggregate: case Opcode::call_indirect_void:
    case Opcode::jump: case Opcode::branch_i1:
    case Opcode::return_i32: case Opcode::return_i64: case Opcode::return_f32:
    case Opcode::return_f64: case Opcode::return_void: case Opcode::return_aggregate:
        return false;
    default:
        return true;
    }
}

void add_use(RegisterBitSet& live, VirtualRegister reg) {
    if (reg < live.size()) live.set(reg);
}

} // namespace

LivenessAnalysis analyze_liveness(const Function& function, bool include_instruction_liveness) {
    LivenessAnalysis analysis;
    const auto block_count = function.blocks.size();
    analysis.uses.assign(block_count, RegisterBitSet(function.register_count));
    analysis.defs.assign(block_count, RegisterBitSet(function.register_count));
    analysis.live_in.assign(block_count, RegisterBitSet(function.register_count));
    analysis.live_out.assign(block_count, RegisterBitSet(function.register_count));
    analysis.successors.resize(block_count);

    std::unordered_map<std::string, std::size_t> block_indices;
    block_indices.reserve(block_count);
    for (std::size_t index = 0; index < block_count; ++index)
        block_indices.emplace(function.blocks[index].name, index);

    for (std::size_t block_index = 0; block_index < block_count; ++block_index) {
        const auto& block = function.blocks[block_index];
        for (const auto parameter : block.parameters)
            if (parameter < function.register_count) analysis.defs[block_index].set(parameter);
        for (const auto& instruction : block.instructions) {
            const auto record_use = [&](VirtualRegister reg) {
                if (reg < function.register_count && !analysis.defs[block_index].test(reg))
                    analysis.uses[block_index].set(reg);
            };
            for (const auto input : instruction.inputs) record_use(input);
            for (const auto& successor : instruction.successors) {
                for (const auto argument : successor.arguments) record_use(argument);
                const auto target = block_indices.find(successor.block);
                if (target != block_indices.end()) analysis.successors[block_index].push_back(target->second);
            }
            if (has_result(instruction.opcode) && instruction.result < function.register_count)
                analysis.defs[block_index].set(instruction.result);
        }
    }

    // Propagate only the reverse-CFG frontier after the initial sweep. Most
    // compiler-generated CFGs converge locally; rescanning every unrelated
    // block on every fixed-point round turns a small loop/backedge update into
    // whole-function work.
    std::vector<std::vector<std::size_t>> predecessors(block_count);
    for (std::size_t source = 0; source < block_count; ++source)
        for (const auto successor : analysis.successors[source])
            predecessors[successor].push_back(source);

    std::vector<bool> dirty(block_count, true);
    std::vector<bool> next_dirty(block_count, false);
    RegisterBitSet next_out(function.register_count);
    RegisterBitSet next_in(function.register_count);
    bool has_dirty = block_count != 0U;
    while (has_dirty) {
        has_dirty = false;
        std::fill(next_dirty.begin(), next_dirty.end(), false);
        ++analysis.fixed_point_iterations;
        for (std::size_t reverse = block_count; reverse > 0; --reverse) {
            const auto block_index = reverse - 1U;
            if (!dirty[block_index]) continue;

            next_out.clear();
            for (const auto successor : analysis.successors[block_index])
                next_out.union_with(analysis.live_in[successor]);

            next_in.assign_union_minus(analysis.uses[block_index], next_out, analysis.defs[block_index]);

            if (next_out == analysis.live_out[block_index] && next_in == analysis.live_in[block_index]) continue;
            analysis.live_out[block_index] = next_out;
            analysis.live_in[block_index] = next_in;
            for (const auto predecessor : predecessors[block_index]) {
                next_dirty[predecessor] = true;
                has_dirty = true;
            }
        }
        dirty.swap(next_dirty);
    }

    // Interval construction needs only block boundary liveness. Avoid the much
    // larger block x instruction x register live-after matrix unless a caller
    // is performing an instruction-local transform such as DCE or call-range
    // splitting.
    if (include_instruction_liveness) {
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
                    live.reset(instruction.result);
                for (const auto input : instruction.inputs) add_use(live, input);
                for (const auto& successor : instruction.successors)
                    for (const auto argument : successor.arguments) add_use(live, argument);
            }
        }
    }

    for (std::size_t block_index = 0; block_index < block_count; ++block_index) {
        analysis.cross_block_live_values += static_cast<std::uint32_t>(analysis.live_out[block_index].count());
        for (const auto& instruction : function.blocks[block_index].instructions)
            for (const auto& successor : instruction.successors)
                analysis.cross_block_live_values += static_cast<std::uint32_t>(successor.arguments.size());
    }
    return analysis;
}

} // namespace forge::machine
