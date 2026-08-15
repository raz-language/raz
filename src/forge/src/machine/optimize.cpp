// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/machine/optimize.hpp"
#include "forge/machine/liveness.hpp"

#include <algorithm>
#include <limits>
#include <numeric>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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

bool is_removable_when_dead(Opcode opcode) noexcept {
    switch (opcode) {
    case Opcode::store_stack_i8: case Opcode::store_stack_i16: case Opcode::store_stack_i32:
    case Opcode::store_stack_i64: case Opcode::store_stack_f32: case Opcode::store_stack_f64:
    case Opcode::store_ptr_i8: case Opcode::store_ptr_i16: case Opcode::store_ptr_i32:
    case Opcode::store_ptr_i64: case Opcode::store_ptr_f32: case Opcode::store_ptr_f64:
    case Opcode::load_ptr_i8: case Opcode::load_ptr_i16: case Opcode::load_ptr_i32:
    case Opcode::load_ptr_i64: case Opcode::load_ptr_f32: case Opcode::load_ptr_f64:
    case Opcode::call_i32: case Opcode::call_i64: case Opcode::call_f32: case Opcode::call_f64:
    case Opcode::call_void: case Opcode::call_aggregate: case Opcode::call_indirect_i32: case Opcode::call_indirect_i64:
    case Opcode::call_indirect_f32: case Opcode::call_indirect_f64: case Opcode::call_indirect_void:
    case Opcode::div_s_i32: case Opcode::div_s_i64: case Opcode::div_u_i32: case Opcode::div_u_i64:
    case Opcode::rem_s_i32: case Opcode::rem_s_i64: case Opcode::rem_u_i32: case Opcode::rem_u_i64:
    case Opcode::jump: case Opcode::branch_i1:
    case Opcode::return_i32: case Opcode::return_i64: case Opcode::return_f32:
    case Opcode::return_f64: case Opcode::return_void: case Opcode::return_aggregate:
        return false;
    default:
        return true;
    }
}

bool is_comparison(Opcode opcode) noexcept {
    return (opcode >= Opcode::cmp_eq_f32 && opcode <= Opcode::cmp_ge_f64) ||
           (opcode >= Opcode::cmp_eq_i32 && opcode <= Opcode::cmp_uge_i64);
}

bool is_integer_comparison(Opcode opcode) noexcept {
    return opcode >= Opcode::cmp_eq_i32 && opcode <= Opcode::cmp_uge_i64;
}

bool is_floating_relational_comparison(Opcode opcode) noexcept {
    return opcode == Opcode::cmp_lt_f32 || opcode == Opcode::cmp_le_f32 ||
           opcode == Opcode::cmp_gt_f32 || opcode == Opcode::cmp_ge_f32 ||
           opcode == Opcode::cmp_lt_f64 || opcode == Opcode::cmp_le_f64 ||
           opcode == Opcode::cmp_gt_f64 || opcode == Opcode::cmp_ge_f64;
}

bool is_copy(Opcode opcode) noexcept {
    return opcode == Opcode::copy || opcode == Opcode::copy_f32 || opcode == Opcode::copy_f64;
}

bool is_redundant_cast(const Instruction& instruction) noexcept {
    if (instruction.opcode != Opcode::zero_extend && instruction.opcode != Opcode::sign_extend &&
        instruction.opcode != Opcode::truncate) return false;
    const auto source_bits = static_cast<unsigned>((instruction.immediate >> 8U) & 0xffU);
    const auto result_bits = static_cast<unsigned>(instruction.immediate & 0xffU);
    return source_bits != 0U && source_bits == result_bits;
}

VirtualRegister resolve_alias(std::vector<VirtualRegister>& aliases, VirtualRegister reg) {
    if (reg >= aliases.size()) return reg;
    auto root = reg;
    while (aliases[root] != root) root = aliases[root];
    while (aliases[reg] != reg) {
        const auto next = aliases[reg];
        aliases[reg] = root;
        reg = next;
    }
    return root;
}

bool compatible(const Function& function, VirtualRegister result, VirtualRegister source) noexcept {
    return result < function.register_count && source < function.register_count &&
           function.register_widths[result] == function.register_widths[source] &&
           function.register_classes[result] == function.register_classes[source];
}

bool supports_integer_immediate(Opcode opcode) noexcept {
    switch (opcode) {
    case Opcode::add_i32: case Opcode::add_i64:
    case Opcode::sub_i32: case Opcode::sub_i64:
    case Opcode::mul_i32: case Opcode::mul_i64:
    case Opcode::and_i32: case Opcode::and_i64:
    case Opcode::or_i32: case Opcode::or_i64:
    case Opcode::xor_i32: case Opcode::xor_i64:
    case Opcode::shl_i32: case Opcode::shl_i64:
    case Opcode::shr_s_i32: case Opcode::shr_s_i64:
    case Opcode::shr_u_i32: case Opcode::shr_u_i64:
        return true;
    default:
        return false;
    }
}

bool is_commutative_integer(Opcode opcode) noexcept {
    return opcode == Opcode::add_i32 || opcode == Opcode::add_i64 ||
           opcode == Opcode::mul_i32 || opcode == Opcode::mul_i64 ||
           opcode == Opcode::and_i32 || opcode == Opcode::and_i64 ||
           opcode == Opcode::or_i32 || opcode == Opcode::or_i64 ||
           opcode == Opcode::xor_i32 || opcode == Opcode::xor_i64;
}

bool is_shift(Opcode opcode) noexcept {
    return opcode == Opcode::shl_i32 || opcode == Opcode::shl_i64 ||
           opcode == Opcode::shr_s_i32 || opcode == Opcode::shr_s_i64 ||
           opcode == Opcode::shr_u_i32 || opcode == Opcode::shr_u_i64;
}

bool is_pointer_memory_operation(Opcode opcode) noexcept {
    switch (opcode) {
    case Opcode::load_ptr_i8: case Opcode::load_ptr_i16: case Opcode::load_ptr_i32:
    case Opcode::load_ptr_i64: case Opcode::load_ptr_f32: case Opcode::load_ptr_f64:
    case Opcode::store_ptr_i8: case Opcode::store_ptr_i16: case Opcode::store_ptr_i32:
    case Opcode::store_ptr_i64: case Opcode::store_ptr_f32: case Opcode::store_ptr_f64:
        return true;
    default:
        return false;
    }
}

std::size_t pointer_operand_index(Opcode opcode) noexcept {
    switch (opcode) {
    case Opcode::store_ptr_i8: case Opcode::store_ptr_i16: case Opcode::store_ptr_i32:
    case Opcode::store_ptr_i64: case Opcode::store_ptr_f32: case Opcode::store_ptr_f64:
        return 1U;
    default:
        return 0U;
    }
}

} // namespace

OptimizationStats optimize_function(Function& function) {
    OptimizationStats stats;
    for (const auto& block : function.blocks)
        stats.instructions_before += static_cast<std::uint32_t>(block.instructions.size());

    // Record copy opportunities whose producer lives in another block. Machine SSA
    // guarantees a single definition, so the existing alias propagation can safely
    // rewrite these uses across block boundaries.
    std::vector<std::size_t> defining_block(function.register_count, function.blocks.size());
    for (std::size_t block_index = 0; block_index < function.blocks.size(); ++block_index) {
        for (const auto parameter : function.blocks[block_index].parameters)
            if (parameter < defining_block.size()) defining_block[parameter] = block_index;
        for (const auto& instruction : function.blocks[block_index].instructions)
            if (has_result(instruction.opcode) && instruction.result < defining_block.size())
                defining_block[instruction.result] = block_index;
    }

    for (std::size_t block_index = 0; block_index < function.blocks.size(); ++block_index) {
        for (const auto& instruction : function.blocks[block_index].instructions) {
            if (!is_copy(instruction.opcode) || instruction.inputs.size() != 1U ||
                instruction.result >= function.register_count) continue;
            const auto source = instruction.inputs.front();
            if (source < defining_block.size() && defining_block[source] != function.blocks.size() &&
                defining_block[source] != block_index && compatible(function, instruction.result, source))
                ++stats.cross_block_copies_propagated;
        }
    }

    // Thread parameterless forwarding blocks before instruction-level cleanup.
    // These blocks carry no values and contain only an unconditional jump, so
    // redirecting incoming edges preserves SSA edge semantics exactly.
    std::unordered_map<std::string, std::string> forwarding;
    for (std::size_t index = 1; index < function.blocks.size(); ++index) {
        const auto& block = function.blocks[index];
        if (!block.parameters.empty() || block.instructions.size() != 1U) continue;
        const auto& terminator = block.instructions.front();
        if (terminator.opcode != Opcode::jump || terminator.successors.size() != 1U ||
            !terminator.successors.front().arguments.empty() ||
            terminator.successors.front().block == block.name) continue;
        forwarding.emplace(block.name, terminator.successors.front().block);
    }
    const auto resolve_forwarding = [&](const std::string& initial) {
        std::string target = initial;
        std::unordered_set<std::string> seen;
        while (seen.insert(target).second) {
            const auto next = forwarding.find(target);
            if (next == forwarding.end()) break;
            target = next->second;
        }
        return target;
    };
    for (auto& block : function.blocks) {
        for (auto& instruction : block.instructions) {
            for (auto& successor : instruction.successors) {
                const auto target = resolve_forwarding(successor.block);
                if (target != successor.block) {
                    successor.block = target;
                    ++stats.jump_threads;
                }
            }
        }
    }

    // Remove forwarding blocks that no longer have incoming edges.
    if (!forwarding.empty()) {
        std::unordered_set<std::string> referenced;
        for (const auto& block : function.blocks)
            for (const auto& instruction : block.instructions)
                for (const auto& successor : instruction.successors)
                    referenced.insert(successor.block);
        std::vector<Block> retained_blocks;
        retained_blocks.reserve(function.blocks.size());
        for (std::size_t index = 0; index < function.blocks.size(); ++index) {
            auto& block = function.blocks[index];
            if (index != 0U && forwarding.contains(block.name) && !referenced.contains(block.name)) {
                ++stats.empty_blocks_removed;
                continue;
            }
            retained_blocks.push_back(std::move(block));
        }
        function.blocks = std::move(retained_blocks);
    }

    // Delete unreachable blocks after threading. The first block is the entry.
    if (!function.blocks.empty()) {
        std::unordered_map<std::string, std::size_t> block_indices;
        for (std::size_t index = 0; index < function.blocks.size(); ++index)
            block_indices.emplace(function.blocks[index].name, index);
        std::vector<bool> reachable(function.blocks.size(), false);
        std::vector<std::size_t> worklist{0U};
        reachable[0] = true;
        while (!worklist.empty()) {
            const auto index = worklist.back();
            worklist.pop_back();
            for (const auto& instruction : function.blocks[index].instructions) {
                for (const auto& successor : instruction.successors) {
                    const auto found = block_indices.find(successor.block);
                    if (found == block_indices.end() || reachable[found->second]) continue;
                    reachable[found->second] = true;
                    worklist.push_back(found->second);
                }
            }
        }
        std::vector<Block> retained_blocks;
        retained_blocks.reserve(function.blocks.size());
        for (std::size_t index = 0; index < function.blocks.size(); ++index) {
            if (!reachable[index]) {
                ++stats.unreachable_blocks_removed;
                continue;
            }
            retained_blocks.push_back(std::move(function.blocks[index]));
        }
        function.blocks = std::move(retained_blocks);
    }

    // Trace-schedule reachable blocks so likely successor edges become physical
    // fallthroughs. This is deliberately frequency-neutral: it follows the sole
    // successor of jumps and the false successor of branches (matching conventional
    // forward-not-taken layout), then appends any
    // remaining blocks in their original order for deterministic output.
    if (function.blocks.size() > 1U) {
        std::unordered_map<std::string, std::size_t> block_indices;
        for (std::size_t index = 0; index < function.blocks.size(); ++index)
            block_indices.emplace(function.blocks[index].name, index);
        std::vector<bool> scheduled(function.blocks.size(), false);
        std::vector<std::size_t> order;
        order.reserve(function.blocks.size());
        const auto schedule_trace = [&](std::size_t start) {
            auto current = start;
            while (current < function.blocks.size() && !scheduled[current]) {
                scheduled[current] = true;
                order.push_back(current);
                const auto& instructions = function.blocks[current].instructions;
                if (instructions.empty()) break;
                const auto& terminator = instructions.back();
                if ((terminator.opcode != Opcode::jump && terminator.opcode != Opcode::branch_i1) ||
                    terminator.successors.empty()) break;
                const auto preferred_successor = terminator.opcode == Opcode::branch_i1 && terminator.successors.size() == 2U
                    ? 1U : 0U;
                const auto found = block_indices.find(terminator.successors[preferred_successor].block);
                if (found == block_indices.end() || scheduled[found->second]) break;
                current = found->second;
            }
        };
        schedule_trace(0U);
        for (std::size_t index = 0; index < function.blocks.size(); ++index)
            if (!scheduled[index]) schedule_trace(index);
        bool changed_order = false;
        for (std::size_t index = 0; index < order.size(); ++index)
            changed_order = changed_order || order[index] != index;
        if (changed_order) {
            std::vector<Block> reordered;
            reordered.reserve(function.blocks.size());
            for (const auto index : order) reordered.push_back(std::move(function.blocks[index]));
            function.blocks = std::move(reordered);
            stats.blocks_reordered = 1U;
        }
    }

    // Rotate simple two-block loops into entry, body, header, exit order.  The
    // loop body then falls through to the header, while the header emits the
    // sole conditional backedge to the body.  This removes the unconditional
    // jump from the hot path without duplicating blocks or changing CFG edges.
    // Restrict the transform to a single-predecessor body so placing it before
    // the header cannot create an accidental fallthrough from another block.
    if (function.blocks.size() > 2U) {
        std::unordered_map<std::string, std::size_t> indices;
        std::unordered_map<std::string, std::uint32_t> predecessors;
        for (std::size_t index = 0; index < function.blocks.size(); ++index)
            indices.emplace(function.blocks[index].name, index);
        for (const auto& block : function.blocks)
            for (const auto& instruction : block.instructions)
                for (const auto& successor : instruction.successors)
                    ++predecessors[successor.block];

        bool rotated = true;
        while (rotated) {
            rotated = false;
            indices.clear();
            for (std::size_t index = 0; index < function.blocks.size(); ++index)
                indices.emplace(function.blocks[index].name, index);
            for (std::size_t header_index = 1U; header_index < function.blocks.size(); ++header_index) {
                const auto& header = function.blocks[header_index];
                if (header.instructions.empty()) continue;
                const auto& branch = header.instructions.back();
                if (branch.opcode != Opcode::branch_i1 || branch.successors.size() != 2U) continue;
                for (const auto& successor : branch.successors) {
                    const auto body_found = indices.find(successor.block);
                    if (body_found == indices.end() || body_found->second == 0U) continue;
                    const auto body_index = body_found->second;
                    const auto& body = function.blocks[body_index];
                    if (body.instructions.empty() || predecessors[body.name] != 1U) continue;
                    const auto& jump = body.instructions.back();
                    if (jump.opcode != Opcode::jump || jump.successors.size() != 1U ||
                        jump.successors.front().block != header.name) continue;
                    if (body_index + 1U == header_index) continue;

                    auto moved = std::move(function.blocks[body_index]);
                    function.blocks.erase(function.blocks.begin() + static_cast<std::ptrdiff_t>(body_index));
                    auto adjusted_header = header_index;
                    if (body_index < header_index) --adjusted_header;
                    function.blocks.insert(function.blocks.begin() + static_cast<std::ptrdiff_t>(adjusted_header),
                                           std::move(moved));
                    stats.blocks_reordered = 1U;
                    rotated = true;
                    break;
                }
                if (rotated) break;
            }
        }
    }

    std::vector<VirtualRegister> aliases(function.register_count);
    std::iota(aliases.begin(), aliases.end(), VirtualRegister{0});
    std::vector<bool> folded_address(function.register_count, false);
    std::vector<bool> eliminated_extension(function.register_count, false);

    // Fold a single-use pointer offset into the displacement field of the
    // consuming load/store. x86-64 can encode base+disp32 directly, avoiding
    // a temporary virtual register and a separate add instruction.
    std::vector<std::uint32_t> use_counts(function.register_count, 0U);
    // Track the sole instruction consumer of each SSA value while counting uses.
    // Several machine peepholes only care about single-use values; keeping this
    // table avoids repeatedly rescanning every block/instruction in the function.
    std::vector<Instruction*> single_instruction_consumer(function.register_count, nullptr);
    for (auto& block : function.blocks) {
        for (auto& instruction : block.instructions) {
            for (const auto input : instruction.inputs) {
                if (input >= use_counts.size()) continue;
                ++use_counts[input];
                if (use_counts[input] == 1U) single_instruction_consumer[input] = &instruction;
                else single_instruction_consumer[input] = nullptr;
            }
            for (const auto& successor : instruction.successors) {
                for (const auto argument : successor.arguments) {
                    if (argument >= use_counts.size()) continue;
                    ++use_counts[argument];
                    // Edge arguments are not instruction consumers for the local
                    // folding transforms below.
                    single_instruction_consumer[argument] = nullptr;
                }
            }
        }
    }

    // Fold a single-use integer constant into arithmetic and shift instructions.
    // This removes the constant-producing virtual register before allocation and
    // lets x86-64 select its native immediate forms directly.
    std::vector<Instruction*> definitions(function.register_count, nullptr);
    std::vector<std::size_t> definition_blocks(function.register_count, function.blocks.size());
    std::vector<std::size_t> definition_instructions(function.register_count, 0U);
    for (std::size_t block_index = 0; block_index < function.blocks.size(); ++block_index) {
        auto& block = function.blocks[block_index];
        for (std::size_t instruction_index = 0; instruction_index < block.instructions.size(); ++instruction_index) {
            auto& instruction = block.instructions[instruction_index];
            if (has_result(instruction.opcode) && instruction.result < definitions.size()) {
                definitions[instruction.result] = &instruction;
                definition_blocks[instruction.result] = block_index;
                definition_instructions[instruction.result] = instruction_index;
            }
        }
    }

    std::vector<bool> eliminated_constant(function.register_count, false);
    std::vector<std::uint32_t> folded_constant_uses(function.register_count, 0U);
    std::vector<std::uint32_t> immediate_capable_uses(function.register_count, 0U);

    // Strength-reduce unsigned division and remainder by a power-of-two constant.
    // This is valid for every unsigned input and replaces the expensive DIV
    // instruction with a shift or mask. Keep the constant definition until all
    // of its uses have been folded so mixed-use constants remain correct.
    for (auto& block : function.blocks) {
        for (auto& instruction : block.instructions) {
            const bool unsigned_division = instruction.opcode == Opcode::div_u_i32 ||
                                           instruction.opcode == Opcode::div_u_i64;
            const bool unsigned_remainder = instruction.opcode == Opcode::rem_u_i32 ||
                                            instruction.opcode == Opcode::rem_u_i64;
            if ((!unsigned_division && !unsigned_remainder) || instruction.inputs.size() != 2U) continue;
            const auto divisor_register = instruction.inputs[1];
            const auto* divisor_definition = divisor_register < definitions.size()
                ? definitions[divisor_register] : nullptr;
            if (divisor_definition == nullptr ||
                (divisor_definition->opcode != Opcode::load_immediate &&
                 divisor_definition->opcode != Opcode::load_immediate_i64)) continue;
            const auto divisor = static_cast<std::uint64_t>(divisor_definition->immediate);
            if (divisor == 0U || (divisor & (divisor - 1U)) != 0U) continue;

            const bool wide = instruction.opcode == Opcode::div_u_i64 ||
                              instruction.opcode == Opcode::rem_u_i64;
            instruction.inputs.resize(1U);
            instruction.symbol = "$imm";
            if (unsigned_division) {
                std::uint32_t shift = 0U;
                auto value = divisor;
                while (value > 1U) { value >>= 1U; ++shift; }
                instruction.opcode = wide ? Opcode::shr_u_i64 : Opcode::shr_u_i32;
                instruction.immediate = static_cast<std::int64_t>(shift);
            } else {
                instruction.opcode = wide ? Opcode::and_i64 : Opcode::and_i32;
                instruction.immediate = static_cast<std::int64_t>(divisor - 1U);
            }
            ++folded_constant_uses[divisor_register];
            ++stats.immediate_forms_selected;
        }
    }

    for (const auto& block : function.blocks) {
        for (const auto& instruction : block.instructions) {
            if (supports_integer_immediate(instruction.opcode) && instruction.inputs.size() == 2U) {
                const auto right = instruction.inputs[1];
                if (right < definitions.size() && definitions[right] != nullptr &&
                    (definitions[right]->opcode == Opcode::load_immediate ||
                     definitions[right]->opcode == Opcode::load_immediate_i64))
                    ++immediate_capable_uses[right];
                if (is_commutative_integer(instruction.opcode)) {
                    const auto left = instruction.inputs[0];
                    if (left < definitions.size() && definitions[left] != nullptr &&
                        (definitions[left]->opcode == Opcode::load_immediate ||
                         definitions[left]->opcode == Opcode::load_immediate_i64))
                        ++immediate_capable_uses[left];
                }
            } else if (is_integer_comparison(instruction.opcode) && instruction.inputs.size() == 2U) {
                const auto right = instruction.inputs[1];
                if (right < definitions.size() && definitions[right] != nullptr &&
                    (definitions[right]->opcode == Opcode::load_immediate ||
                     definitions[right]->opcode == Opcode::load_immediate_i64))
                    ++immediate_capable_uses[right];
            }
        }
    }

    std::vector<bool> eliminated_load(function.register_count, false);
    for (auto& block : function.blocks) {
        for (auto& instruction : block.instructions) {
            if (!supports_integer_immediate(instruction.opcode) || instruction.inputs.size() != 2U) continue;
            std::size_t constant_index = 1U;
            auto reg = instruction.inputs[constant_index];
            auto* definition = reg < definitions.size() ? definitions[reg] : nullptr;
            if ((definition == nullptr ||
                 (definition->opcode != Opcode::load_immediate && definition->opcode != Opcode::load_immediate_i64)) &&
                is_commutative_integer(instruction.opcode)) {
                constant_index = 0U;
                reg = instruction.inputs[constant_index];
                definition = reg < definitions.size() ? definitions[reg] : nullptr;
            }
            if (definition == nullptr ||
                (definition->opcode != Opcode::load_immediate && definition->opcode != Opcode::load_immediate_i64)) continue;
            const auto value = definition->immediate;
            const bool wide = instruction.opcode == Opcode::add_i64 || instruction.opcode == Opcode::sub_i64 ||
                              instruction.opcode == Opcode::mul_i64 || instruction.opcode == Opcode::and_i64 ||
                              instruction.opcode == Opcode::or_i64 || instruction.opcode == Opcode::xor_i64 ||
                              instruction.opcode == Opcode::shl_i64 || instruction.opcode == Opcode::shr_s_i64 ||
                              instruction.opcode == Opcode::shr_u_i64;
            if (is_shift(instruction.opcode)) {
                if (value < 0 || value > 255) continue;
            } else if (wide && (value < std::numeric_limits<std::int32_t>::min() ||
                                value > std::numeric_limits<std::int32_t>::max())) {
                continue;
            }
            if (constant_index == 0U) std::swap(instruction.inputs[0], instruction.inputs[1]);
            instruction.inputs.resize(1U);
            instruction.immediate = value;
            instruction.symbol = "$imm";
            ++folded_constant_uses[reg];
            ++stats.immediate_forms_selected;
        }
    }

    // Fold a single-use right-hand integer constant into comparisons. The
    // comparison remains an SSA definition until branch fusion below, but no
    // virtual register or allocation is needed for the constant operand.
    for (auto& block : function.blocks) {
        for (auto& instruction : block.instructions) {
            if (!is_integer_comparison(instruction.opcode) || instruction.inputs.size() != 2U) continue;
            const auto constant = instruction.inputs[1];
            auto* definition = constant < definitions.size() ? definitions[constant] : nullptr;
            if (definition == nullptr ||
                (definition->opcode != Opcode::load_immediate && definition->opcode != Opcode::load_immediate_i64)) continue;
            const auto value = definition->immediate;
            const bool wide = instruction.opcode >= Opcode::cmp_eq_i64;
            if (wide && (value < std::numeric_limits<std::int32_t>::min() ||
                         value > std::numeric_limits<std::int32_t>::max())) continue;
            instruction.inputs.resize(1U);
            instruction.immediate = value;
            instruction.symbol = "$cmpimm";
            ++folded_constant_uses[constant];
            ++stats.immediate_comparisons_selected;
        }
    }

    // A constant may be folded into only some of its uses. Keep its materialization
    // for edge arguments, ABI uses, or unsupported operations, and remove it only
    // when every original use has been rewritten. This shortens arithmetic live
    // ranges without corrupting mixed-use constants such as loop initializers.
    for (VirtualRegister reg = 0; reg < function.register_count; ++reg) {
        if (use_counts[reg] != 0U && folded_constant_uses[reg] == use_counts[reg]) {
            eliminated_constant[reg] = true;
            ++stats.constant_definitions_eliminated;
        }
    }

    // Fold single-use integer constants directly into stack and pointer stores.
    // x86-64 supports immediate-to-memory encodings for i8/i16/i32 and for i64
    // values representable as a sign-extended imm32. The pointer operand remains
    // live for pointer stores; only the constant value register disappears.
    for (auto& block : function.blocks) {
        for (auto& instruction : block.instructions) {
            const bool stack_store = instruction.opcode == Opcode::store_stack_i8 ||
                                     instruction.opcode == Opcode::store_stack_i16 ||
                                     instruction.opcode == Opcode::store_stack_i32 ||
                                     instruction.opcode == Opcode::store_stack_i64;
            const bool pointer_store = instruction.opcode == Opcode::store_ptr_i8 ||
                                       instruction.opcode == Opcode::store_ptr_i16 ||
                                       instruction.opcode == Opcode::store_ptr_i32 ||
                                       instruction.opcode == Opcode::store_ptr_i64;
            if ((!stack_store && !pointer_store) || instruction.inputs.empty()) continue;
            const auto value_reg = instruction.inputs.front();
            auto* definition = value_reg < definitions.size() ? definitions[value_reg] : nullptr;
            if (definition == nullptr || use_counts[value_reg] != 1U ||
                (definition->opcode != Opcode::load_immediate && definition->opcode != Opcode::load_immediate_i64)) continue;
            const auto value = definition->immediate;
            const bool wide = instruction.opcode == Opcode::store_stack_i64 || instruction.opcode == Opcode::store_ptr_i64;
            if (wide && (value < std::numeric_limits<std::int32_t>::min() ||
                         value > std::numeric_limits<std::int32_t>::max())) continue;
            instruction.inputs.erase(instruction.inputs.begin());
            instruction.argument_index = static_cast<std::uint32_t>(value);
            instruction.symbol = "$storeimm";
            eliminated_constant[value_reg] = true;
            ++stats.constant_stores_selected;
            ++stats.constant_definitions_eliminated;
        }
    }

    // Materialize single-use integer constants directly in the ABI return
    // register. Zero uses xor eax,eax, avoiding both a virtual register and a
    // longer mov-immediate encoding.
    for (auto& block : function.blocks) {
        for (auto& instruction : block.instructions) {
            if ((instruction.opcode != Opcode::return_i32 && instruction.opcode != Opcode::return_i64) ||
                instruction.inputs.size() != 1U) continue;
            const auto value_reg = instruction.inputs.front();
            auto* definition = value_reg < definitions.size() ? definitions[value_reg] : nullptr;
            if (definition == nullptr || use_counts[value_reg] != 1U ||
                (definition->opcode != Opcode::load_immediate && definition->opcode != Opcode::load_immediate_i64)) continue;
            instruction.inputs.clear();
            instruction.immediate = definition->immediate;
            instruction.symbol = "$retimm";
            eliminated_constant[value_reg] = true;
            ++stats.direct_constant_returns;
            ++stats.constant_definitions_eliminated;
            if (instruction.immediate == 0) ++stats.zeroing_idioms_selected;
        }
    }

    // Fold a single-use local stack load directly into an integer return. The
    // ABI return register is the natural destination of the memory load, so the
    // temporary virtual register and its allocation are unnecessary. Narrow
    // i8/i16 loads retain their zero-extension semantics.
    for (auto& block : function.blocks) {
        for (auto& instruction : block.instructions) {
            if ((instruction.opcode != Opcode::return_i32 && instruction.opcode != Opcode::return_i64) ||
                instruction.inputs.size() != 1U) continue;
            const auto value_reg = instruction.inputs.front();
            auto* definition = value_reg < definitions.size() ? definitions[value_reg] : nullptr;
            if (definition == nullptr || use_counts[value_reg] != 1U) continue;
            std::uint32_t width = 0U;
            switch (definition->opcode) {
            case Opcode::load_stack_i8: width = 1U; break;
            case Opcode::load_stack_i16: width = 2U; break;
            case Opcode::load_stack_i32: width = 4U; break;
            case Opcode::load_stack_i64: width = 8U; break;
            default: continue;
            }
            if ((instruction.opcode == Opcode::return_i64 && width != 8U) ||
                (instruction.opcode == Opcode::return_i32 && width == 8U)) continue;
            instruction.inputs.clear();
            instruction.immediate = definition->immediate;
            instruction.argument_index = width;
            instruction.symbol = "$retloadstack";
            eliminated_load[value_reg] = true;
            ++stats.load_returns_folded;
        }
    }

    // Fold a single-use local-stack load into the right-hand memory operand of
    // integer arithmetic. x86-64 can consume [rbp+disp32] directly, removing
    // the temporary load result before liveness and register allocation.
    for (auto& block : function.blocks) {
        for (auto& instruction : block.instructions) {
            const bool supported = instruction.opcode == Opcode::add_i32 || instruction.opcode == Opcode::add_i64 ||
                                   instruction.opcode == Opcode::sub_i32 || instruction.opcode == Opcode::sub_i64 ||
                                   instruction.opcode == Opcode::mul_i32 || instruction.opcode == Opcode::mul_i64 ||
                                   instruction.opcode == Opcode::and_i32 || instruction.opcode == Opcode::and_i64 ||
                                   instruction.opcode == Opcode::or_i32 || instruction.opcode == Opcode::or_i64 ||
                                   instruction.opcode == Opcode::xor_i32 || instruction.opcode == Opcode::xor_i64;
            if (!supported || instruction.inputs.size() != 2U || !instruction.symbol.empty()) continue;
            const auto loaded_reg = instruction.inputs[1];
            auto* definition = loaded_reg < definitions.size() ? definitions[loaded_reg] : nullptr;
            if (definition == nullptr || use_counts[loaded_reg] != 1U) continue;
            const bool wide = instruction.opcode == Opcode::add_i64 || instruction.opcode == Opcode::sub_i64 ||
                              instruction.opcode == Opcode::mul_i64 || instruction.opcode == Opcode::and_i64 ||
                              instruction.opcode == Opcode::or_i64 || instruction.opcode == Opcode::xor_i64;
            if ((wide && definition->opcode != Opcode::load_stack_i64) ||
                (!wide && definition->opcode != Opcode::load_stack_i32)) continue;
            instruction.inputs.resize(1U);
            instruction.immediate = definition->immediate;
            instruction.symbol = "$memstack";
            eliminated_load[loaded_reg] = true;
            ++stats.load_arithmetic_folded;
        }
    }

    // Fuse a comparison used only by a conditional branch. The branch retains
    // the original comparison operands and condition in its immediate field,
    // allowing the encoder to emit cmp+jcc directly without materializing i1.
    std::vector<bool> fused_compare(function.register_count, false);
    std::vector<bool> eliminated_test_mask(function.register_count, false);
    for (auto& block : function.blocks) {
        for (auto& select : block.instructions) {
            if ((select.opcode != Opcode::select_i32 && select.opcode != Opcode::select_i64) ||
                select.inputs.size() != 3U) continue;
            const auto condition = select.inputs[0];
            if (condition >= function.register_count || use_counts[condition] != 1U) continue;
            auto* comparison = condition < definitions.size() ? definitions[condition] : nullptr;
            if (comparison == nullptr || comparison->symbol != "$cmpimm" || comparison->immediate != 0 ||
                (comparison->opcode != Opcode::cmp_eq_i32 && comparison->opcode != Opcode::cmp_ne_i32 &&
                 comparison->opcode != Opcode::cmp_eq_i64 && comparison->opcode != Opcode::cmp_ne_i64) ||
                comparison->inputs.size() != 1U) continue;
            const auto masked = comparison->inputs[0];
            auto* producer = masked < definitions.size() ? definitions[masked] : nullptr;
            if (producer == nullptr || (producer->opcode != Opcode::and_i32 && producer->opcode != Opcode::and_i64) ||
                producer->symbol != "$imm" || producer->inputs.size() != 1U || use_counts[masked] != 1U ||
                producer->immediate < std::numeric_limits<std::int32_t>::min() ||
                producer->immediate > std::numeric_limits<std::int32_t>::max()) continue;
            select.inputs[0] = producer->inputs[0];
            select.symbol = "$testimm";
            select.argument_index = static_cast<std::uint32_t>(comparison->opcode) + 1U;
            select.immediate = producer->immediate;
            fused_compare[condition] = true;
            eliminated_test_mask[masked] = true;
        }
    }

    for (auto& block : function.blocks) {
        for (auto& branch : block.instructions) {
            if (branch.opcode != Opcode::branch_i1 || branch.inputs.size() != 1U) continue;
            const auto condition = branch.inputs.front();
            if (condition >= function.register_count || use_counts[condition] != 1U) continue;
            Instruction* comparison = condition < definitions.size() ? definitions[condition] : nullptr;
            if (comparison == nullptr || !is_integer_comparison(comparison->opcode) ||
                (comparison->inputs.size() != 2U &&
                 !(comparison->inputs.size() == 1U && comparison->symbol == "$cmpimm"))) continue;
            branch.inputs = comparison->inputs;
            bool reuse_arithmetic_flags = false;
            bool use_test_immediate = false;
            if (comparison->symbol == "$cmpimm" && comparison->immediate == 0 &&
                (comparison->opcode == Opcode::cmp_eq_i32 || comparison->opcode == Opcode::cmp_ne_i32 ||
                 comparison->opcode == Opcode::cmp_eq_i64 || comparison->opcode == Opcode::cmp_ne_i64) &&
                !comparison->inputs.empty()) {
                const auto masked = comparison->inputs.front();
                auto* producer = masked < definitions.size() ? definitions[masked] : nullptr;
                if (producer != nullptr &&
                    (producer->opcode == Opcode::and_i32 || producer->opcode == Opcode::and_i64) &&
                    producer->symbol == "$imm" && producer->inputs.size() == 1U &&
                    masked < use_counts.size() && use_counts[masked] == 1U &&
                    producer->immediate >= std::numeric_limits<std::int32_t>::min() &&
                    producer->immediate <= std::numeric_limits<std::int32_t>::max()) {
                    branch.inputs = producer->inputs;
                    branch.symbol = "$testimm";
                    branch.argument_index = static_cast<std::uint32_t>(comparison->opcode) + 1U;
                    branch.immediate = producer->immediate;
                    eliminated_test_mask[masked] = true;
                    use_test_immediate = true;
                }
            }
            if (comparison->symbol == "$cmpimm" && comparison->immediate == 0 &&
                (comparison->opcode == Opcode::cmp_eq_i32 || comparison->opcode == Opcode::cmp_ne_i32 ||
                 comparison->opcode == Opcode::cmp_eq_i64 || comparison->opcode == Opcode::cmp_ne_i64)) {
                const auto comparison_block = definition_blocks[condition];
                const auto comparison_index = definition_instructions[condition];
                if (comparison_block < function.blocks.size() &&
                    &function.blocks[comparison_block] == &block &&
                    comparison_index > 0U && comparison_index + 1U < block.instructions.size() &&
                    &block.instructions[comparison_index + 1U] == &branch) {
                    const auto& producer = block.instructions[comparison_index - 1U];
                    const bool flag_setting_integer = producer.opcode == Opcode::add_i32 || producer.opcode == Opcode::add_i64 ||
                        producer.opcode == Opcode::sub_i32 || producer.opcode == Opcode::sub_i64 ||
                        producer.opcode == Opcode::and_i32 || producer.opcode == Opcode::and_i64 ||
                        producer.opcode == Opcode::or_i32 || producer.opcode == Opcode::or_i64 ||
                        producer.opcode == Opcode::xor_i32 || producer.opcode == Opcode::xor_i64;
                    reuse_arithmetic_flags = flag_setting_integer && !comparison->inputs.empty() &&
                        producer.result == comparison->inputs.front();
                }
            }
            if (use_test_immediate) {
                // Already encoded above as a direct TEST of the original value.
            } else if (reuse_arithmetic_flags) {
                branch.symbol = "$flags";
                branch.argument_index = static_cast<std::uint32_t>(comparison->opcode) + 1U;
                branch.immediate = 0;
            } else if (comparison->symbol == "$cmpimm") {
                branch.symbol = "$cmpimm";
                branch.argument_index = static_cast<std::uint32_t>(comparison->opcode) + 1U;
                branch.immediate = comparison->immediate;
            } else {
                branch.immediate = static_cast<std::int64_t>(comparison->opcode) + 1;
            }
            fused_compare[condition] = true;
            ++stats.compare_branches_fused;
            stats.compare_branch_bytes_avoided += 8U;
        }
    }

    // Fuse ordered floating relational comparisons into branches.  Equality
    // and inequality need an explicit PF (unordered/NaN) combination, so they
    // remain materialized until the branch representation can express two
    // flag predicates.  Relational comparisons can be made NaN-correct with
    // a single unsigned condition by selecting the operand order carefully.
    for (auto& block : function.blocks) {
        for (auto& branch : block.instructions) {
            if (branch.opcode != Opcode::branch_i1 || branch.inputs.size() != 1U) continue;
            const auto condition = branch.inputs.front();
            if (condition >= function.register_count || use_counts[condition] != 1U) continue;
            Instruction* comparison = condition < definitions.size() ? definitions[condition] : nullptr;
            if (comparison == nullptr || !is_floating_relational_comparison(comparison->opcode) ||
                comparison->inputs.size() != 2U) continue;
            branch.inputs = comparison->inputs;
            branch.symbol = "$fcmp";
            branch.argument_index = static_cast<std::uint32_t>(comparison->opcode) + 1U;
            branch.immediate = 0;
            fused_compare[condition] = true;
            ++stats.floating_compare_branches_fused;
            stats.floating_compare_branch_bytes_avoided += 8U;
        }
    }

    for (auto& block : function.blocks) {
        for (auto& definition : block.instructions) {
            if (definition.opcode != Opcode::ptr_offset || definition.inputs.size() != 1U ||
                definition.result >= function.register_count || use_counts[definition.result] != 1U) continue;
            Instruction* consumer = definition.result < single_instruction_consumer.size()
                ? single_instruction_consumer[definition.result] : nullptr;
            if (consumer == nullptr || !is_pointer_memory_operation(consumer->opcode)) continue;
            const auto operand_index = pointer_operand_index(consumer->opcode);
            if (consumer->inputs.size() <= operand_index || consumer->inputs[operand_index] != definition.result) continue;
            const auto displacement = definition.immediate + consumer->immediate;
            if (displacement < std::numeric_limits<std::int32_t>::min() ||
                displacement > std::numeric_limits<std::int32_t>::max()) continue;
            consumer->inputs[operand_index] = definition.inputs.front();
            consumer->immediate = displacement;
            definition.immediate = 0;
            folded_address[definition.result] = true;
        }
    }

    // Eliminate inverse extension chains such as truncate(zero_extend(x)) and
    // truncate(sign_extend(x)) when the truncate restores the original width.
    // The high bits introduced by the extension are discarded, so the chain is
    // exactly equivalent to the original SSA value.
    for (const auto& block : function.blocks) {
        for (const auto& instruction : block.instructions) {
            if (instruction.opcode != Opcode::truncate || instruction.inputs.size() != 1U ||
                instruction.result >= function.register_count) continue;
            const auto extended = instruction.inputs.front();
            const auto* definition = extended < definitions.size() ? definitions[extended] : nullptr;
            if (definition == nullptr || definition->inputs.size() != 1U ||
                (definition->opcode != Opcode::zero_extend && definition->opcode != Opcode::sign_extend)) continue;
            const auto original_bits = static_cast<unsigned>((definition->immediate >> 8U) & 0xffU);
            const auto extended_bits = static_cast<unsigned>(definition->immediate & 0xffU);
            const auto truncate_source_bits = static_cast<unsigned>((instruction.immediate >> 8U) & 0xffU);
            const auto truncate_result_bits = static_cast<unsigned>(instruction.immediate & 0xffU);
            if (original_bits == 0U || extended_bits == 0U || truncate_source_bits != extended_bits ||
                truncate_result_bits != original_bits) continue;
            const auto original = definition->inputs.front();
            if (!compatible(function, instruction.result, original)) continue;
            aliases[instruction.result] = original;
            if (extended < use_counts.size() && use_counts[extended] == 1U) eliminated_extension[extended] = true;
        }
    }

    // Machine IR is SSA. Alias-producing operations can therefore be collected
    // independently of block layout and applied transitively to every use.
    for (const auto& block : function.blocks) {
        for (const auto& instruction : block.instructions) {
            if (instruction.inputs.size() != 1U || instruction.result >= function.register_count) continue;
            const auto source = instruction.inputs.front();
            if (!compatible(function, instruction.result, source)) continue;
            if (is_copy(instruction.opcode) ||
                (instruction.opcode == Opcode::ptr_offset && instruction.immediate == 0) ||
                is_redundant_cast(instruction)) {
                aliases[instruction.result] = source;
            }
        }
    }

    for (VirtualRegister reg = 0; reg < function.register_count; ++reg)
        aliases[reg] = resolve_alias(aliases, reg);

    for (auto& block : function.blocks) {
        std::vector<Instruction> optimized;
        optimized.reserve(block.instructions.size());
        for (auto instruction : block.instructions) {
            for (auto& input : instruction.inputs) input = resolve_alias(aliases, input);
            for (auto& successor : instruction.successors)
                for (auto& argument : successor.arguments) argument = resolve_alias(aliases, argument);

            if (instruction.result < fused_compare.size() && fused_compare[instruction.result] &&
                is_integer_comparison(instruction.opcode)) {
                continue;
            }
            if (instruction.result < eliminated_test_mask.size() && eliminated_test_mask[instruction.result] &&
                (instruction.opcode == Opcode::and_i32 || instruction.opcode == Opcode::and_i64)) {
                continue;
            }
            if (instruction.result < eliminated_constant.size() && eliminated_constant[instruction.result] &&
                (instruction.opcode == Opcode::load_immediate || instruction.opcode == Opcode::load_immediate_i64)) {
                continue;
            }
            if (instruction.result < eliminated_load.size() && eliminated_load[instruction.result] &&
                (instruction.opcode == Opcode::load_stack_i8 || instruction.opcode == Opcode::load_stack_i16 ||
                 instruction.opcode == Opcode::load_stack_i32 || instruction.opcode == Opcode::load_stack_i64)) {
                continue;
            }
            if (instruction.result < eliminated_extension.size() && eliminated_extension[instruction.result] &&
                (instruction.opcode == Opcode::zero_extend || instruction.opcode == Opcode::sign_extend)) {
                ++stats.extension_chains_eliminated;
                continue;
            }

            const bool alias_definition = instruction.inputs.size() == 1U &&
                instruction.result < function.register_count &&
                aliases[instruction.result] != instruction.result;
            if (alias_definition) {
                if (is_copy(instruction.opcode)) ++stats.copies_propagated;
                else if (instruction.opcode == Opcode::ptr_offset && instruction.immediate == 0 &&
                         instruction.result < folded_address.size() && folded_address[instruction.result])
                    ++stats.address_modes_folded;
                else if (instruction.opcode == Opcode::ptr_offset && instruction.immediate == 0)
                    ++stats.zero_offsets_eliminated;
                else if (is_redundant_cast(instruction)) ++stats.redundant_casts_eliminated;
                else if (instruction.opcode == Opcode::truncate) ++stats.extension_chains_eliminated;
                else optimized.push_back(std::move(instruction));
                continue;
            }
            optimized.push_back(std::move(instruction));
        }
        block.instructions = std::move(optimized);
    }

    // Global machine dead-code elimination. Recompute full CFG liveness after
    // each sweep so chains spanning block boundaries collapse to a fixed point.
    // Potentially trapping operations and memory/call side effects are retained.
    bool removed_dead_code = true;
    while (removed_dead_code) {
        removed_dead_code = false;
        const auto liveness = analyze_liveness(function);
        stats.liveness_iterations += liveness.fixed_point_iterations;
        stats.cross_block_live_values = std::max(stats.cross_block_live_values,
                                                  liveness.cross_block_live_values);
        for (std::size_t block_index = 0; block_index < function.blocks.size(); ++block_index) {
            auto& instructions = function.blocks[block_index].instructions;
            std::vector<Instruction> retained_in_reverse;
            retained_in_reverse.reserve(instructions.size());
            for (std::size_t reverse = instructions.size(); reverse > 0; --reverse) {
                const auto instruction_index = reverse - 1U;
                auto& instruction = instructions[instruction_index];
                const bool result_is_dead = has_result(instruction.opcode) &&
                    instruction.result < function.register_count &&
                    !liveness.live_after[block_index][instruction_index][instruction.result];
                if (result_is_dead && is_removable_when_dead(instruction.opcode)) {
                    ++stats.dead_instructions_eliminated;
                    if (is_comparison(instruction.opcode)) ++stats.dead_comparisons_eliminated;
                    removed_dead_code = true;
                    continue;
                }
                retained_in_reverse.push_back(std::move(instruction));
            }
            std::reverse(retained_in_reverse.begin(), retained_in_reverse.end());
            instructions = std::move(retained_in_reverse);
        }
    }

    stats.instructions_after = 0;
    for (const auto& block : function.blocks)
        stats.instructions_after += static_cast<std::uint32_t>(block.instructions.size());

    // Remove virtual-register holes left by erased alias definitions. Keeping
    // the machine IR dense preserves the verifier invariant that every virtual
    // register has exactly one definition and avoids allocating dead aliases.
    std::vector<bool> retained(function.register_count, false);
    for (const auto& block : function.blocks) {
        for (const auto parameter : block.parameters) if (parameter < retained.size()) retained[parameter] = true;
        for (const auto& instruction : block.instructions) {
            if (instruction.result < retained.size()) {
                const bool has_result = instruction.opcode != Opcode::jump && instruction.opcode != Opcode::branch_i1 &&
                    instruction.opcode != Opcode::return_i32 && instruction.opcode != Opcode::return_i64 &&
                    instruction.opcode != Opcode::return_f32 && instruction.opcode != Opcode::return_f64 &&
                    instruction.opcode != Opcode::return_void && instruction.opcode != Opcode::return_aggregate && instruction.opcode != Opcode::call_void && instruction.opcode != Opcode::call_aggregate &&
                    instruction.opcode != Opcode::call_indirect_void &&
                    instruction.opcode != Opcode::store_stack_i8 && instruction.opcode != Opcode::store_stack_i16 &&
                    instruction.opcode != Opcode::store_stack_i32 && instruction.opcode != Opcode::store_stack_i64 &&
                    instruction.opcode != Opcode::store_stack_f32 && instruction.opcode != Opcode::store_stack_f64 &&
                    instruction.opcode != Opcode::store_ptr_i8 && instruction.opcode != Opcode::store_ptr_i16 &&
                    instruction.opcode != Opcode::store_ptr_i32 && instruction.opcode != Opcode::store_ptr_i64 &&
                    instruction.opcode != Opcode::store_ptr_f32 && instruction.opcode != Opcode::store_ptr_f64;
                if (has_result) retained[instruction.result] = true;
            }
        }
    }

    std::vector<VirtualRegister> remap(function.register_count, function.register_count);
    std::vector<std::uint8_t> widths;
    std::vector<RegisterClass> classes;
    widths.reserve(function.register_count);
    classes.reserve(function.register_count);
    for (VirtualRegister reg = 0; reg < function.register_count; ++reg) {
        if (!retained[reg]) continue;
        remap[reg] = static_cast<VirtualRegister>(widths.size());
        widths.push_back(function.register_widths[reg]);
        classes.push_back(function.register_classes[reg]);
    }

    for (auto& block : function.blocks) {
        for (auto& parameter : block.parameters) parameter = remap[parameter];
        for (auto& instruction : block.instructions) {
            for (auto& input : instruction.inputs) input = remap[input];
            for (auto& successor : instruction.successors)
                for (auto& argument : successor.arguments) argument = remap[argument];
            if (instruction.result < remap.size() && remap[instruction.result] != function.register_count)
                instruction.result = remap[instruction.result];
        }
    }
    function.register_count = static_cast<VirtualRegister>(widths.size());
    function.register_widths = std::move(widths);
    function.register_classes = std::move(classes);

    function.machine_instructions_before_optimization = stats.instructions_before;
    function.machine_copies_propagated = stats.copies_propagated;
    function.machine_zero_offsets_eliminated = stats.zero_offsets_eliminated;
    function.machine_redundant_casts_eliminated = stats.redundant_casts_eliminated;
    function.machine_address_modes_folded = stats.address_modes_folded;
    function.machine_compare_branches_fused = stats.compare_branches_fused;
    function.machine_compare_branch_bytes_avoided = stats.compare_branch_bytes_avoided;
    function.machine_floating_compare_branches_fused = stats.floating_compare_branches_fused;
    function.machine_floating_compare_branch_bytes_avoided = stats.floating_compare_branch_bytes_avoided;
    function.machine_jump_threads = stats.jump_threads;
    function.machine_empty_blocks_removed = stats.empty_blocks_removed;
    function.machine_unreachable_blocks_removed = stats.unreachable_blocks_removed;
    function.machine_blocks_reordered = stats.blocks_reordered;
    function.machine_immediate_forms_selected = stats.immediate_forms_selected;
    function.machine_constant_definitions_eliminated = stats.constant_definitions_eliminated;
    function.machine_immediate_comparisons_selected = stats.immediate_comparisons_selected;
    function.machine_direct_constant_returns = stats.direct_constant_returns;
    function.machine_zeroing_idioms_selected = stats.zeroing_idioms_selected;
    function.machine_constant_stores_selected = stats.constant_stores_selected;
    function.machine_extension_chains_eliminated = stats.extension_chains_eliminated;
    function.machine_load_returns_folded = stats.load_returns_folded;
    function.machine_load_arithmetic_folded = stats.load_arithmetic_folded;
    function.machine_dead_instructions_eliminated = stats.dead_instructions_eliminated;
    function.machine_dead_comparisons_eliminated = stats.dead_comparisons_eliminated;
    function.machine_cross_block_copies_propagated = stats.cross_block_copies_propagated;
    function.machine_liveness_iterations = stats.liveness_iterations;
    function.machine_cross_block_live_values = stats.cross_block_live_values;
    return stats;
}

OptimizationStats optimize_module(Module& module) {
    OptimizationStats total;
    for (auto& function : module.functions) {
        const auto stats = optimize_function(function);
        total.instructions_before += stats.instructions_before;
        total.instructions_after += stats.instructions_after;
        total.copies_propagated += stats.copies_propagated;
        total.zero_offsets_eliminated += stats.zero_offsets_eliminated;
        total.redundant_casts_eliminated += stats.redundant_casts_eliminated;
        total.address_modes_folded += stats.address_modes_folded;
        total.compare_branches_fused += stats.compare_branches_fused;
        total.compare_branch_bytes_avoided += stats.compare_branch_bytes_avoided;
        total.floating_compare_branches_fused += stats.floating_compare_branches_fused;
        total.floating_compare_branch_bytes_avoided += stats.floating_compare_branch_bytes_avoided;
        total.jump_threads += stats.jump_threads;
        total.empty_blocks_removed += stats.empty_blocks_removed;
        total.unreachable_blocks_removed += stats.unreachable_blocks_removed;
        total.blocks_reordered += stats.blocks_reordered;
        total.immediate_forms_selected += stats.immediate_forms_selected;
        total.constant_definitions_eliminated += stats.constant_definitions_eliminated;
        total.immediate_comparisons_selected += stats.immediate_comparisons_selected;
        total.direct_constant_returns += stats.direct_constant_returns;
        total.zeroing_idioms_selected += stats.zeroing_idioms_selected;
        total.constant_stores_selected += stats.constant_stores_selected;
        total.extension_chains_eliminated += stats.extension_chains_eliminated;
        total.load_returns_folded += stats.load_returns_folded;
        total.load_arithmetic_folded += stats.load_arithmetic_folded;
        total.dead_instructions_eliminated += stats.dead_instructions_eliminated;
        total.dead_comparisons_eliminated += stats.dead_comparisons_eliminated;
        total.cross_block_copies_propagated += stats.cross_block_copies_propagated;
        total.liveness_iterations += stats.liveness_iterations;
        total.cross_block_live_values += stats.cross_block_live_values;
    }
    return total;
}

} // namespace forge::machine
