// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/machine/optimize.hpp"
#include "forge/machine/liveness.hpp"

#include <algorithm>
#include <functional>
#include <limits>
#include <numeric>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace forge::machine {

SlpCostModel SlpCostModel::x86_64(X86VectorIsa isa) noexcept {
    SlpCostModel model;
    model.sse2 = true;
    model.vector_bits = 128;
    model.backend_vector_bits = isa == X86VectorIsa::avx512 ? 512 :
        (isa == X86VectorIsa::avx2 ? 256 : 128);
    model.vector_register_budget = 12;

    switch (isa) {
    case X86VectorIsa::sse2:
        break;
    case X86VectorIsa::sse41:
        model.sse41 = true;
        model.shuffle_cost = 0.70;
        break;
    case X86VectorIsa::avx:
        model.sse41 = true;
        model.avx = true;
        // AVX1 does not widen packed integer arithmetic beyond 128 bits.
        model.vector_bits = 128;
        model.shuffle_cost = 0.65;
        model.vector_setup_cost = 0.16;
        break;
    case X86VectorIsa::avx2:
        model.sse41 = true;
        model.avx = true;
        model.avx2 = true;
        model.vector_bits = 256;
        model.vector_integer_throughput = 0.33;
        model.vector_memory_cost = 0.85;
        model.broadcast_cost = 0.40;
        model.shuffle_cost = 0.55;
        model.vector_setup_cost = 0.14;
        break;
    case X86VectorIsa::avx512:
        model.sse41 = true;
        model.avx = true;
        model.avx2 = true;
        model.avx512f = true;
        model.avx512bw = true;
        model.avx512vl = true;
        model.vector_bits = 512;
        model.vector_register_budget = 28;
        model.mask_register_budget = 7;
        model.vector_integer_throughput = 0.30;
        model.vector_memory_cost = 0.82;
        model.broadcast_cost = 0.35;
        model.shuffle_cost = 0.50;
        model.vector_setup_cost = 0.12;
        break;
    }
    return model;
}

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

struct SlpCandidateCost {
    std::size_t lane_bytes{};
    std::size_t lanes{};
    std::size_t operations_per_lane{1U};
    std::size_t loads_per_lane{1U};
    std::size_t stores_per_lane{1U};
    std::size_t vector_live_values{2U};
    Opcode primary_opcode{Opcode::add_i64};
    SlpMemoryPattern memory_pattern{SlpMemoryPattern::contiguous_unaligned};
    std::size_t broadcasts_per_chunk{};
    std::size_t shuffles_per_chunk{};
    double operation_mix_multiplier{1.0};
};

double slp_memory_multiplier(SlpMemoryPattern pattern, const SlpCostModel& model) noexcept {
    switch (pattern) {
    case SlpMemoryPattern::contiguous_aligned: return model.aligned_memory_multiplier;
    case SlpMemoryPattern::contiguous_unaligned: return model.unaligned_memory_multiplier;
    case SlpMemoryPattern::broadcast: return 1.0;
    case SlpMemoryPattern::interleaved: return model.interleaved_memory_multiplier;
    case SlpMemoryPattern::strided: return model.strided_memory_multiplier;
    case SlpMemoryPattern::gather_scatter: return model.gather_scatter_multiplier;
    }
    return 1.0;
}

double slp_operation_multiplier(Opcode opcode, const SlpCostModel& model) noexcept {
    switch (opcode) {
    case Opcode::and_i32: case Opcode::and_i64:
    case Opcode::or_i32: case Opcode::or_i64:
    case Opcode::xor_i32: case Opcode::xor_i64:
        return 0.80;
    case Opcode::add_i32: case Opcode::add_i64:
    case Opcode::sub_i32: case Opcode::sub_i64:
        return 1.00;
    case Opcode::mul_i32:
        return model.sse41 ? 1.35 : 2.25;
    case Opcode::mul_i64:
        // No native packed i64 multiply in SSE2/AVX2; treat it as expensive
        // unless a future backend teaches the vectorizer a lowering sequence.
        return model.avx512f ? 2.0 : 4.0;
    case Opcode::shl_i32: case Opcode::shl_i64:
    case Opcode::shr_s_i32: case Opcode::shr_s_i64:
    case Opcode::shr_u_i32: case Opcode::shr_u_i64:
        return 1.25;
    default:
        return 1.0;
    }
}

std::uint16_t slp_selected_vector_bits(std::size_t lane_bytes, std::size_t lanes, const SlpCostModel& model) noexcept {
    const auto total_bits = lane_bytes * lanes * 8U;
    const auto effective = static_cast<std::size_t>(model.effective_vector_bits());
    const auto usable = std::min(total_bits, effective);
    if (usable >= 512U) return 512U;
    if (usable >= 256U) return 256U;
    return 128U;
}

bool slp_profitable(const SlpCandidateCost& candidate, const SlpCostModel& model, OptimizationStats& stats) noexcept {
    ++stats.slp_candidates_considered;
    const auto effective_bits = model.effective_vector_bits();
    if (!model.vector_integer_available || !model.sse2 || effective_bits < 64U || candidate.lane_bytes == 0U || candidate.lanes < 2U) {
        ++stats.slp_candidates_rejected_target;
        return false;
    }
    const auto vector_bytes = static_cast<std::size_t>(effective_bits / 8U);
    if (vector_bytes < candidate.lane_bytes) {
        ++stats.slp_candidates_rejected_target;
        return false;
    }

    const auto total_bytes = candidate.lane_bytes * candidate.lanes;
    const auto chunks = (total_bytes + vector_bytes - 1U) / vector_bytes;
    const auto op_multiplier = slp_operation_multiplier(candidate.primary_opcode, model) * candidate.operation_mix_multiplier;
    const double scalar_op = (model.scalar_integer_latency + model.scalar_integer_throughput) * op_multiplier;
    const double vector_op = (model.vector_integer_latency + model.vector_integer_throughput) * op_multiplier;
    const double memory_multiplier = slp_memory_multiplier(candidate.memory_pattern, model);

    const double scalar = static_cast<double>(candidate.lanes) *
        (static_cast<double>(candidate.operations_per_lane) * scalar_op +
         static_cast<double>(candidate.loads_per_lane + candidate.stores_per_lane) * model.scalar_memory_cost);
    const double memory = static_cast<double>(chunks) *
        static_cast<double>(candidate.loads_per_lane + candidate.stores_per_lane) * model.vector_memory_cost * memory_multiplier;
    const double shuffle = static_cast<double>(chunks) *
        (static_cast<double>(candidate.broadcasts_per_chunk) * model.broadcast_cost +
         static_cast<double>(candidate.shuffles_per_chunk) * model.shuffle_cost);
    const double pressure = candidate.vector_live_values > model.vector_register_budget
        ? static_cast<double>(candidate.vector_live_values - model.vector_register_budget) * model.register_pressure_cost * static_cast<double>(chunks)
        : 0.0;
    const double vector = static_cast<double>(chunks) *
        (static_cast<double>(candidate.operations_per_lane) * vector_op + model.vector_setup_cost) + memory + shuffle + pressure;

    stats.slp_estimated_scalar_cost += scalar;
    stats.slp_estimated_vector_cost += vector;
    stats.slp_estimated_memory_cost += memory;
    stats.slp_estimated_shuffle_cost += shuffle;
    stats.slp_estimated_register_pressure_cost += pressure;

    if (!(vector * model.minimum_speedup < scalar)) {
        ++stats.slp_candidates_rejected_cost;
        return false;
    }
    ++stats.slp_candidates_selected;
    const auto selected_bits = slp_selected_vector_bits(candidate.lane_bytes, candidate.lanes, model);
    if (selected_bits >= 512U) ++stats.slp_width_512_selected;
    else if (selected_bits >= 256U) ++stats.slp_width_256_selected;
    else ++stats.slp_width_128_selected;
    return true;
}

} // namespace

OptimizationStats optimize_function(Function& function, const SlpCostModel& slp_cost_model) {
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

    // Recognize straight-line in-place integer expression packs over
    // contiguous lanes. The scalar operation must be one of the SSE2-safe
    // lane-wise operations whose scalar source is shared by every lane:
    // add/sub/and/or/xor for i32 or i64. Each scalar result must feed only its
    // matching store, and the arithmetic/store groups must be adjacent so no
    // intervening memory side effect can change aliasing semantics.
    //
    // This generalizes the original i64 map-add recognizer into a small SLP
    // packer while deliberately avoiding multiplication, shifts, or floating
    // point where target support or reassociation semantics are different.
    {
        std::vector<std::uint32_t> uses(function.register_count, 0U);
        struct ScalarLoadInfo { bool valid{}; VirtualRegister base{}; std::int64_t offset{}; Opcode opcode{}; };
        std::vector<ScalarLoadInfo> scalar_loads(function.register_count);
        for (const auto& block : function.blocks) {
            for (const auto& ins : block.instructions) {
                for (const auto input : ins.inputs) if (input < uses.size()) ++uses[input];
                if ((ins.opcode == Opcode::load_ptr_i32 || ins.opcode == Opcode::load_ptr_i64) &&
                    ins.result < scalar_loads.size() && ins.inputs.size() == 1U)
                    scalar_loads[ins.result] = {true, ins.inputs[0], ins.immediate, ins.opcode};
            }
        }
        std::unordered_set<VirtualRegister> packed_scalar_loads;

        const auto lane_width = [](Opcode opcode) -> std::size_t {
            switch (opcode) {
            case Opcode::add_i32: case Opcode::sub_i32: case Opcode::and_i32: case Opcode::or_i32: case Opcode::xor_i32: return 4U;
            case Opcode::add_i64: case Opcode::sub_i64: case Opcode::and_i64: case Opcode::or_i64: case Opcode::xor_i64: return 8U;
            default: return 0U;
            }
        };
        const auto matching_store = [](Opcode arithmetic) -> Opcode {
            switch (arithmetic) {
            case Opcode::add_i32: case Opcode::sub_i32: case Opcode::and_i32: case Opcode::or_i32: case Opcode::xor_i32:
                return Opcode::store_ptr_i32;
            case Opcode::add_i64: case Opcode::sub_i64: case Opcode::and_i64: case Opcode::or_i64: case Opcode::xor_i64:
                return Opcode::store_ptr_i64;
            default: return Opcode::jump; // impossible sentinel for unsupported arithmetic
            }
        };

        for (auto& block : function.blocks) {
            auto& ins = block.instructions;
            for (std::size_t start = 0; start < ins.size(); ++start) {
                bool matched = false;
                for (const std::size_t lanes : {16U, 8U, 4U, 2U}) {
                    if (start + lanes * 2U > ins.size()) continue;
                    const auto arithmetic_opcode = ins[start].opcode;
                    const auto bytes = lane_width(arithmetic_opcode);
                    if (bytes == 0U) continue;
                    const auto store_opcode = matching_store(arithmetic_opcode);
                    VirtualRegister base = function.register_count;
                    VirtualRegister store_base = function.register_count;
                    VirtualRegister scalar = function.register_count;
                    bool legal = true;
                    std::vector<VirtualRegister> candidate_scalar_loads;
                    for (std::size_t lane = 0; lane < lanes; ++lane) {
                        const auto& arithmetic = ins[start + lane];
                        const auto& store = ins[start + lanes + lane];
                        if (arithmetic.opcode != arithmetic_opcode || arithmetic.inputs.size() != 2U ||
                            store.opcode != store_opcode || store.inputs.size() != 2U ||
                            store.inputs[0] != arithmetic.result || arithmetic.result >= uses.size() ||
                            uses[arithmetic.result] != 1U ||
                            store.immediate != static_cast<std::int64_t>(lane * bytes)) {
                            legal = false; break;
                        }
                        VirtualRegister lane_scalar = function.register_count;
                        VirtualRegister lane_base = function.register_count;
                        std::int64_t lane_offset = 0;
                        VirtualRegister scalar_load_result = function.register_count;
                        if (arithmetic.symbol == "$memptr") {
                            lane_scalar = arithmetic.inputs[0];
                            lane_base = arithmetic.inputs[1];
                            lane_offset = arithmetic.immediate;
                        } else if (arithmetic.symbol.empty() &&
                                   (arithmetic_opcode == Opcode::sub_i32 || arithmetic_opcode == Opcode::sub_i64)) {
                            scalar_load_result = arithmetic.inputs[0];
                            if (scalar_load_result >= scalar_loads.size() || !scalar_loads[scalar_load_result].valid ||
                                uses[scalar_load_result] != 1U ||
                                scalar_loads[scalar_load_result].opcode !=
                                    (bytes == 4U ? Opcode::load_ptr_i32 : Opcode::load_ptr_i64)) {
                                legal = false; break;
                            }
                            lane_scalar = arithmetic.inputs[1];
                            lane_base = scalar_loads[scalar_load_result].base;
                            lane_offset = scalar_loads[scalar_load_result].offset;
                        } else {
                            legal = false; break;
                        }
                        if (lane_offset != static_cast<std::int64_t>(lane * bytes)) { legal = false; break; }
                        const auto lane_store_base = store.inputs[1];
                        if (lane == 0U) {
                            base = lane_base;
                            store_base = lane_store_base;
                            scalar = lane_scalar;
                        } else if (base != lane_base || store_base != lane_store_base || scalar != lane_scalar) {
                            legal = false; break;
                        }
                        if (scalar_load_result < function.register_count)
                            candidate_scalar_loads.push_back(scalar_load_result);
                    }
                    if (!legal) continue;
                    if (!slp_profitable({bytes, lanes, 1U, 1U, 1U, 2U, arithmetic_opcode,
                                         SlpMemoryPattern::contiguous_unaligned, 1U, 0U, 1.0}, slp_cost_model, stats)) continue;
                    packed_scalar_loads.insert(candidate_scalar_loads.begin(), candidate_scalar_loads.end());

                    Instruction packed;
                    packed.vector_bits = slp_selected_vector_bits(bytes, lanes, slp_cost_model);
                    const bool inplace = base == store_base;
                    packed.opcode = bytes == 4U
                        ? (inplace ? Opcode::binary_i32_contiguous_inplace : Opcode::binary_i32_contiguous_map)
                        : (inplace ? Opcode::binary_i64_contiguous_inplace : Opcode::binary_i64_contiguous_map);
                    packed.inputs = inplace ? std::vector<VirtualRegister>{base, scalar}
                                            : std::vector<VirtualRegister>{base, store_base, scalar};
                    packed.immediate = static_cast<std::int64_t>(lanes);
                    packed.argument_index = static_cast<std::uint32_t>(arithmetic_opcode);
                    ins[start] = std::move(packed);
                    ins.erase(ins.begin() + static_cast<std::ptrdiff_t>(start + 1U),
                              ins.begin() + static_cast<std::ptrdiff_t>(start + lanes * 2U));
                    matched = true;
                    break;
                }
                (void)matched;
            }
        }
        if (!packed_scalar_loads.empty()) {
            for (auto& block : function.blocks) {
                auto& instructions = block.instructions;
                instructions.erase(std::remove_if(instructions.begin(), instructions.end(), [&](const Instruction& ins) {
                    return has_result(ins.opcode) && packed_scalar_loads.contains(ins.result);
                }), instructions.end());
            }
        }
    }

    // Recognize packed integer DAGs with shared scalar subexpressions. Unlike
    // the postfix tree form below, this node table gives every packed value an
    // identity so one computed vector can feed multiple parents without being
    // recomputed. Node records are {tag,lhs,rhs} uint16 triples; source tags
    // have bit 15 set and binary nodes reference earlier node IDs.
    {
        std::vector<Instruction*> defs(function.register_count, nullptr);
        std::vector<std::uint32_t> uses(function.register_count, 0U);
        struct LoadInfo { bool valid{}; VirtualRegister base{}; std::int64_t offset{}; Opcode opcode{}; };
        std::vector<LoadInfo> loads(function.register_count);
        for (auto& block : function.blocks) {
            for (auto& instruction : block.instructions) {
                if (has_result(instruction.opcode) && instruction.result < defs.size()) defs[instruction.result] = &instruction;
                for (const auto input : instruction.inputs) if (input < uses.size()) ++uses[input];
                if ((instruction.opcode == Opcode::load_ptr_i32 || instruction.opcode == Opcode::load_ptr_i64) &&
                    instruction.result < loads.size() && instruction.inputs.size() == 1U)
                    loads[instruction.result] = {true, instruction.inputs[0], instruction.immediate, instruction.opcode};
            }
        }
        const auto lane_width = [](Opcode opcode) -> std::size_t {
            switch (opcode) {
            case Opcode::add_i32: case Opcode::sub_i32: case Opcode::and_i32: case Opcode::or_i32: case Opcode::xor_i32: return 4U;
            case Opcode::add_i64: case Opcode::sub_i64: case Opcode::and_i64: case Opcode::or_i64: case Opcode::xor_i64: return 8U;
            default: return 0U;
            }
        };
        struct Node { std::uint16_t tag{}; std::uint16_t lhs{0xffffU}; std::uint16_t rhs{0xffffU}; };
        std::unordered_set<Instruction*> erase_instructions;
        std::unordered_set<VirtualRegister> erase_results;
        for (auto& block : function.blocks) {
            auto& instructions = block.instructions;
            for (std::size_t start = 0; start < instructions.size(); ++start) {
                for (const std::size_t lanes : {16U, 8U, 4U, 2U}) {
                    if (start + lanes > instructions.size()) continue;
                    const auto store_opcode = instructions[start].opcode;
                    if (store_opcode != Opcode::store_ptr_i32 && store_opcode != Opcode::store_ptr_i64) continue;
                    const std::size_t bytes = store_opcode == Opcode::store_ptr_i32 ? 4U : 8U;
                    const auto load_opcode = bytes == 4U ? Opcode::load_ptr_i32 : Opcode::load_ptr_i64;
                    VirtualRegister destination = function.register_count;
                    std::vector<Node> expected_nodes;
                    std::vector<VirtualRegister> expected_sources;
                    std::vector<Instruction*> local_erase;
                    std::vector<VirtualRegister> local_results;
                    bool legal = true;
                    bool expected_shared = false;
                    for (std::size_t lane = 0; lane < lanes && legal; ++lane) {
                        auto& store = instructions[start + lane];
                        const auto offset = static_cast<std::int64_t>(lane * bytes);
                        if (store.opcode != store_opcode || store.inputs.size() != 2U || store.immediate != offset) { legal = false; break; }
                        if (destination == function.register_count) destination = store.inputs[1];
                        else if (destination != store.inputs[1]) { legal = false; break; }

                        std::vector<Node> nodes;
                        std::vector<VirtualRegister> sources;
                        std::unordered_map<VirtualRegister, std::uint16_t> memo;
                        std::unordered_map<VirtualRegister, std::uint32_t> occurrences;
                        std::unordered_map<VirtualRegister, std::uint16_t> source_nodes;
                        std::unordered_map<VirtualRegister, std::uint16_t> source_indices;
                        std::vector<Instruction*> lane_erase;
                        std::vector<VirtualRegister> lane_results;
                        bool shared = false;
                        std::function<std::optional<std::uint16_t>(VirtualRegister)> visit = [&](VirtualRegister value) -> std::optional<std::uint16_t> {
                            ++occurrences[value];
                            if (const auto it = memo.find(value); it != memo.end()) {
                                if (value < defs.size() && defs[value] != nullptr && lane_width(defs[value]->opcode) == bytes) shared = true;
                                return it->second;
                            }
                            if (value < defs.size() && defs[value] != nullptr && lane_width(defs[value]->opcode) == bytes && defs[value]->inputs.size() == 2U) {
                                auto* operation = defs[value];
                                const auto left = visit(operation->inputs[0]);
                                if (!left) return std::nullopt;
                                std::optional<std::uint16_t> right;
                                if (operation->symbol == "$memptr") {
                                    if (operation->immediate != offset) return std::nullopt;
                                    const auto base = operation->inputs[1];
                                    auto [si, inserted] = source_indices.emplace(base, static_cast<std::uint16_t>(sources.size()));
                                    if (inserted) sources.push_back(base);
                                    auto [sn, new_node] = source_nodes.emplace(base, static_cast<std::uint16_t>(nodes.size()));
                                    if (new_node) nodes.push_back(Node{static_cast<std::uint16_t>(0x8000U | si->second), 0xffffU, 0xffffU});
                                    right = sn->second;
                                } else {
                                    right = visit(operation->inputs[1]);
                                    if (!right) return std::nullopt;
                                }
                                if (nodes.size() >= 0x7fffU) return std::nullopt;
                                const auto id = static_cast<std::uint16_t>(nodes.size());
                                nodes.push_back(Node{static_cast<std::uint16_t>(operation->opcode), *left, *right});
                                memo.emplace(value, id);
                                lane_erase.push_back(operation);
                                lane_results.push_back(value);
                                return id;
                            }
                            if (value >= loads.size() || !loads[value].valid || loads[value].opcode != load_opcode || loads[value].offset != offset)
                                return std::nullopt;
                            const auto base = loads[value].base;
                            auto [si, inserted] = source_indices.emplace(base, static_cast<std::uint16_t>(sources.size()));
                            if (inserted) sources.push_back(base);
                            const auto id = static_cast<std::uint16_t>(nodes.size());
                            nodes.push_back(Node{static_cast<std::uint16_t>(0x8000U | si->second), 0xffffU, 0xffffU});
                            memo.emplace(value, id);
                            if (defs[value] != nullptr) lane_erase.push_back(defs[value]);
                            lane_results.push_back(value);
                            return id;
                        };
                        const auto root = visit(store.inputs[0]);
                        if (!root || *root + 1U != nodes.size() || !shared || nodes.size() < 5U) { legal = false; break; }
                        for (const auto& [value, count] : occurrences) {
                            if (value < uses.size() && uses[value] != count) { legal = false; break; }
                        }
                        if (!legal) break;
                        if (lane == 0U) {
                            expected_nodes = nodes;
                            expected_sources = sources;
                            expected_shared = shared;
                        } else if (expected_nodes.size() != nodes.size() || expected_sources != sources || expected_shared != shared) {
                            legal = false; break;
                        } else {
                            for (std::size_t n = 0; n < nodes.size(); ++n) {
                                if (expected_nodes[n].tag != nodes[n].tag || expected_nodes[n].lhs != nodes[n].lhs || expected_nodes[n].rhs != nodes[n].rhs) {
                                    legal = false; break;
                                }
                            }
                            if (!legal) break;
                        }
                        local_erase.insert(local_erase.end(), lane_erase.begin(), lane_erase.end());
                        local_results.insert(local_results.end(), lane_results.begin(), lane_results.end());
                    }
                    if (!legal || expected_sources.empty()) continue;
                    if (!slp_profitable({bytes, lanes, expected_nodes.size(), expected_sources.size(), 1U, 4U, Opcode::add_i64,
                                         SlpMemoryPattern::contiguous_unaligned, 0U, 0U,
                                         [&] {
                                             double sum = 0.0; std::size_t count = 0U;
                                             for (const auto& node : expected_nodes) if ((node.tag & 0x8000U) == 0U) {
                                                 sum += slp_operation_multiplier(static_cast<Opcode>(node.tag), slp_cost_model); ++count;
                                             }
                                             return count == 0U ? 1.0 : sum / static_cast<double>(count);
                                         }()}, slp_cost_model, stats)) continue;
                    Instruction packed;
                    packed.vector_bits = slp_selected_vector_bits(bytes, lanes, slp_cost_model);
                    packed.opcode = bytes == 4U ? Opcode::binary_i32_contiguous_dag_reuse : Opcode::binary_i64_contiguous_dag_reuse;
                    packed.inputs = expected_sources;
                    packed.inputs.push_back(destination);
                    packed.immediate = static_cast<std::int64_t>(lanes);
                    packed.symbol.reserve(expected_nodes.size() * 6U);
                    const auto append16 = [&](std::uint16_t value) {
                        packed.symbol.push_back(static_cast<char>(value & 0xffU));
                        packed.symbol.push_back(static_cast<char>((value >> 8U) & 0xffU));
                    };
                    for (const auto& node : expected_nodes) { append16(node.tag); append16(node.lhs); append16(node.rhs); }
                    instructions[start] = std::move(packed);
                    instructions.erase(instructions.begin() + static_cast<std::ptrdiff_t>(start + 1U),
                                       instructions.begin() + static_cast<std::ptrdiff_t>(start + lanes));
                    for (auto* instruction : local_erase) erase_instructions.insert(instruction);
                    for (const auto value : local_results) erase_results.insert(value);
                    break;
                }
            }
        }
        if (!erase_instructions.empty()) {
            for (auto& block : function.blocks) {
                auto& instructions = block.instructions;
                instructions.erase(std::remove_if(instructions.begin(), instructions.end(), [&](const Instruction& instruction) {
                    return erase_instructions.contains(const_cast<Instruction*>(&instruction)) ||
                           (has_result(instruction.opcode) && erase_results.contains(instruction.result));
                }), instructions.end());
            }
        }
    }

    // Recognize true branching packed expression DAGs over contiguous source
    // arrays. The scalar tree is serialized as a compact postfix program in
    // Instruction::symbol; source leaves reference Instruction::inputs by
    // index. This supports arbitrary binary tree shapes without a fixed depth
    // of map2/map3/mapN pseudo-ops.
    {
        std::vector<Instruction*> defs(function.register_count, nullptr);
        std::vector<std::uint32_t> uses(function.register_count, 0U);
        struct LoadInfo { bool valid{}; VirtualRegister base{}; std::int64_t offset{}; Opcode opcode{}; };
        std::vector<LoadInfo> loads(function.register_count);
        for (auto& block : function.blocks) {
            for (auto& instruction : block.instructions) {
                if (has_result(instruction.opcode) && instruction.result < defs.size()) defs[instruction.result] = &instruction;
                for (const auto input : instruction.inputs) if (input < uses.size()) ++uses[input];
                if ((instruction.opcode == Opcode::load_ptr_i32 || instruction.opcode == Opcode::load_ptr_i64) &&
                    instruction.result < loads.size() && instruction.inputs.size() == 1U)
                    loads[instruction.result] = {true, instruction.inputs[0], instruction.immediate, instruction.opcode};
            }
        }
        const auto lane_width = [](Opcode opcode) -> std::size_t {
            switch (opcode) {
            case Opcode::add_i32: case Opcode::sub_i32: case Opcode::and_i32: case Opcode::or_i32: case Opcode::xor_i32: return 4U;
            case Opcode::add_i64: case Opcode::sub_i64: case Opcode::and_i64: case Opcode::or_i64: case Opcode::xor_i64: return 8U;
            default: return 0U;
            }
        };
        struct DagBuild {
            bool legal{true};
            bool contains_operation{};
            bool branching{};
            std::vector<std::uint16_t> tokens;
            std::vector<VirtualRegister> sources;
            std::vector<Instruction*> erase;
            std::vector<VirtualRegister> erase_results;
            std::size_t operation_count{};
            std::size_t max_stack_depth{};
        };
        std::unordered_set<Instruction*> erase_instructions;
        std::unordered_set<VirtualRegister> erase_results;
        for (auto& block : function.blocks) {
            auto& instructions = block.instructions;
            for (std::size_t start = 0; start < instructions.size(); ++start) {
                for (const std::size_t lanes : {16U, 8U, 4U, 2U}) {
                    if (start + lanes > instructions.size()) continue;
                    const auto store_opcode = instructions[start].opcode;
                    if (store_opcode != Opcode::store_ptr_i32 && store_opcode != Opcode::store_ptr_i64) continue;
                    const std::size_t bytes = store_opcode == Opcode::store_ptr_i32 ? 4U : 8U;
                    const auto load_opcode = bytes == 4U ? Opcode::load_ptr_i32 : Opcode::load_ptr_i64;
                    VirtualRegister destination = function.register_count;
                    std::vector<std::uint16_t> expected_tokens;
                    std::vector<VirtualRegister> expected_sources;
                    std::vector<Instruction*> local_erase;
                    std::vector<VirtualRegister> local_results;
                    bool legal = true;
                    bool expected_branching = false;
                    std::size_t expected_ops = 0U;
                    std::size_t expected_max_depth = 0U;
                    for (std::size_t lane = 0; lane < lanes && legal; ++lane) {
                        auto& store = instructions[start + lane];
                        const auto offset = static_cast<std::int64_t>(lane * bytes);
                        if (store.opcode != store_opcode || store.inputs.size() != 2U || store.immediate != offset) { legal = false; break; }
                        if (destination == function.register_count) destination = store.inputs[1];
                        else if (destination != store.inputs[1]) { legal = false; break; }

                        DagBuild build;
                        std::unordered_map<VirtualRegister, std::uint16_t> source_indices;
                        std::function<bool(VirtualRegister)> visit = [&](VirtualRegister value) -> bool {
                            if (value < defs.size() && defs[value] != nullptr && uses[value] == 1U &&
                                lane_width(defs[value]->opcode) == bytes && defs[value]->inputs.size() == 2U) {
                                auto* operation = defs[value];
                                const auto left = operation->inputs[0];
                                const bool left_is_op = left < defs.size() && defs[left] != nullptr && uses[left] == 1U && lane_width(defs[left]->opcode) == bytes;
                                if (!visit(left)) return false;
                                bool right_is_op = false;
                                if (operation->symbol == "$memptr") {
                                    if (operation->immediate != offset) return false;
                                    const auto base = operation->inputs[1];
                                    auto [it, inserted] = source_indices.emplace(base, static_cast<std::uint16_t>(build.sources.size()));
                                    if (inserted) build.sources.push_back(base);
                                    build.tokens.push_back(static_cast<std::uint16_t>(0x8000U | it->second));
                                } else {
                                    const auto right = operation->inputs[1];
                                    right_is_op = right < defs.size() && defs[right] != nullptr && uses[right] == 1U && lane_width(defs[right]->opcode) == bytes;
                                    if (!visit(right)) return false;
                                }
                                build.tokens.push_back(static_cast<std::uint16_t>(operation->opcode));
                                build.erase.push_back(operation);
                                build.erase_results.push_back(operation->result);
                                ++build.operation_count;
                                build.contains_operation = true;
                                if (left_is_op && right_is_op) build.branching = true;
                                return true;
                            }
                            if (value >= loads.size() || !loads[value].valid || loads[value].opcode != load_opcode ||
                                loads[value].offset != offset || uses[value] != 1U) return false;
                            const auto base = loads[value].base;
                            auto [it, inserted] = source_indices.emplace(base, static_cast<std::uint16_t>(build.sources.size()));
                            if (inserted) build.sources.push_back(base);
                            build.tokens.push_back(static_cast<std::uint16_t>(0x8000U | it->second));
                            if (defs[value] != nullptr) build.erase.push_back(defs[value]);
                            build.erase_results.push_back(value);
                            return true;
                        };
                        const auto result = store.inputs[0];
                        if (!visit(result) || !build.branching || build.operation_count < 3U) { legal = false; break; }
                        std::size_t depth = 0U;
                        for (const auto token : build.tokens) {
                            if ((token & 0x8000U) != 0U) { ++depth; build.max_stack_depth = std::max(build.max_stack_depth, depth); }
                            else { if (depth < 2U) { legal = false; break; } --depth; }
                        }
                        if (!legal || depth != 1U || build.max_stack_depth > 8U) { legal = false; break; }
                        if (lane == 0U) {
                            expected_tokens = build.tokens;
                            expected_sources = build.sources;
                            expected_branching = build.branching;
                            expected_ops = build.operation_count;
                            expected_max_depth = build.max_stack_depth;
                        } else if (expected_tokens != build.tokens || expected_sources != build.sources ||
                                   expected_branching != build.branching || expected_ops != build.operation_count ||
                                   expected_max_depth != build.max_stack_depth) { legal = false; break; }
                        local_erase.insert(local_erase.end(), build.erase.begin(), build.erase.end());
                        local_results.insert(local_results.end(), build.erase_results.begin(), build.erase_results.end());
                    }
                    if (!legal || expected_sources.empty()) continue;
                    if (!slp_profitable({bytes, lanes, expected_ops, expected_sources.size(), 1U, expected_max_depth, Opcode::add_i64,
                                         SlpMemoryPattern::contiguous_unaligned, 0U, 0U,
                                         [&] {
                                             double sum = 0.0; std::size_t count = 0U;
                                             for (const auto token : expected_tokens) if ((token & 0x8000U) == 0U) {
                                                 sum += slp_operation_multiplier(static_cast<Opcode>(token), slp_cost_model); ++count;
                                             }
                                             return count == 0U ? 1.0 : sum / static_cast<double>(count);
                                         }()}, slp_cost_model, stats)) continue;
                    Instruction packed;
                    packed.vector_bits = slp_selected_vector_bits(bytes, lanes, slp_cost_model);
                    packed.opcode = bytes == 4U ? Opcode::binary_i32_contiguous_dag : Opcode::binary_i64_contiguous_dag;
                    packed.inputs = expected_sources;
                    packed.inputs.push_back(destination);
                    packed.immediate = static_cast<std::int64_t>(lanes);
                    packed.symbol.reserve(expected_tokens.size() * 2U);
                    for (const auto token : expected_tokens) {
                        packed.symbol.push_back(static_cast<char>(token & 0xffU));
                        packed.symbol.push_back(static_cast<char>((token >> 8U) & 0xffU));
                    }
                    instructions[start] = std::move(packed);
                    instructions.erase(instructions.begin() + static_cast<std::ptrdiff_t>(start + 1U),
                                       instructions.begin() + static_cast<std::ptrdiff_t>(start + lanes));
                    for (auto* instruction : local_erase) erase_instructions.insert(instruction);
                    for (const auto value : local_results) erase_results.insert(value);
                    break;
                }
            }
        }
        if (!erase_instructions.empty()) {
            for (auto& block : function.blocks) {
                auto& instructions = block.instructions;
                instructions.erase(std::remove_if(instructions.begin(), instructions.end(), [&](const Instruction& instruction) {
                    return erase_instructions.contains(const_cast<Instruction*>(&instruction)) ||
                           (has_result(instruction.opcode) && erase_results.contains(instruction.result));
                }), instructions.end());
            }
        }
    }

    // Recognize arbitrary-depth left-deep vector expression chains over
    // contiguous source arrays and one contiguous destination array:
    //
    //     dst[i] = (((a[i] op0 b[i]) op1 c[i]) op2 d[i]) ...
    //
    // This deliberately requires every scalar load and intermediate to be
    // single-use and every source to use the exact same contiguous lane
    // offsets. The chain length is carried explicitly on the machine
    // instruction, so this optimization has no fixed operation-count ceiling.
    {
        std::vector<Instruction*> defs(function.register_count, nullptr);
        std::vector<std::uint32_t> uses(function.register_count, 0U);
        struct LoadInfo { bool valid{}; VirtualRegister base{}; std::int64_t offset{}; Opcode opcode{}; };
        std::vector<LoadInfo> loads(function.register_count);
        for (auto& block : function.blocks) {
            for (auto& instruction : block.instructions) {
                if (has_result(instruction.opcode) && instruction.result < defs.size()) defs[instruction.result] = &instruction;
                for (const auto input : instruction.inputs) if (input < uses.size()) ++uses[input];
                if ((instruction.opcode == Opcode::load_ptr_i32 || instruction.opcode == Opcode::load_ptr_i64) &&
                    instruction.result < loads.size() && instruction.inputs.size() == 1U)
                    loads[instruction.result] = {true, instruction.inputs[0], instruction.immediate, instruction.opcode};
            }
        }
        const auto lane_width = [](Opcode opcode) -> std::size_t {
            switch (opcode) {
            case Opcode::add_i32: case Opcode::sub_i32: case Opcode::and_i32: case Opcode::or_i32: case Opcode::xor_i32: return 4U;
            case Opcode::add_i64: case Opcode::sub_i64: case Opcode::and_i64: case Opcode::or_i64: case Opcode::xor_i64: return 8U;
            default: return 0U;
            }
        };
        std::unordered_set<Instruction*> erase_instructions;
        std::unordered_set<VirtualRegister> erase_results;
        for (auto& block : function.blocks) {
            auto& instructions = block.instructions;
            for (std::size_t start = 0; start < instructions.size(); ++start) {
                for (const std::size_t lanes : {16U, 8U, 4U, 2U}) {
                    if (start + lanes > instructions.size()) continue;
                    const auto store_opcode = instructions[start].opcode;
                    if (store_opcode != Opcode::store_ptr_i32 && store_opcode != Opcode::store_ptr_i64) continue;
                    const std::size_t bytes = store_opcode == Opcode::store_ptr_i32 ? 4U : 8U;
                    const auto load_opcode = bytes == 4U ? Opcode::load_ptr_i32 : Opcode::load_ptr_i64;
                    VirtualRegister destination = function.register_count;
                    std::vector<VirtualRegister> expected_sources;
                    std::vector<Opcode> expected_operations;
                    std::vector<Instruction*> local_erase;
                    std::vector<VirtualRegister> local_results;
                    bool legal = true;
                    for (std::size_t lane = 0; lane < lanes && legal; ++lane) {
                        auto& store = instructions[start + lane];
                        const auto offset = static_cast<std::int64_t>(lane * bytes);
                        if (store.opcode != store_opcode || store.inputs.size() != 2U || store.immediate != offset) { legal = false; break; }
                        if (destination == function.register_count) destination = store.inputs[1];
                        else if (destination != store.inputs[1]) { legal = false; break; }
                        auto result = store.inputs[0];
                        if (result >= defs.size() || defs[result] == nullptr || uses[result] != 1U) { legal = false; break; }

                        std::vector<Opcode> reversed_operations;
                        std::vector<VirtualRegister> reversed_right_sources;
                        std::vector<Instruction*> lane_erase;
                        std::vector<VirtualRegister> lane_results;
                        VirtualRegister first_source = function.register_count;
                        auto* operation = defs[result];
                        while (operation != nullptr && lane_width(operation->opcode) == bytes && operation->inputs.size() == 2U) {
                            reversed_operations.push_back(operation->opcode);
                            lane_erase.push_back(operation);
                            lane_results.push_back(operation->result);

                            VirtualRegister right_base = function.register_count;
                            if (operation->symbol == "$memptr") {
                                if (operation->immediate != offset) { legal = false; break; }
                                right_base = operation->inputs[1];
                            } else {
                                const auto right = operation->inputs[1];
                                if (right >= loads.size() || !loads[right].valid || loads[right].opcode != load_opcode ||
                                    loads[right].offset != offset || uses[right] != 1U) { legal = false; break; }
                                right_base = loads[right].base;
                                if (defs[right] != nullptr) lane_erase.push_back(defs[right]);
                                lane_results.push_back(right);
                            }
                            reversed_right_sources.push_back(right_base);

                            const auto left = operation->inputs[0];
                            if (left < defs.size() && defs[left] != nullptr && lane_width(defs[left]->opcode) == bytes && uses[left] == 1U) {
                                operation = defs[left];
                                continue;
                            }
                            if (left >= loads.size() || !loads[left].valid || loads[left].opcode != load_opcode ||
                                loads[left].offset != offset || uses[left] != 1U) { legal = false; break; }
                            first_source = loads[left].base;
                            if (defs[left] != nullptr) lane_erase.push_back(defs[left]);
                            lane_results.push_back(left);
                            operation = nullptr;
                        }
                        if (!legal || first_source == function.register_count || reversed_operations.size() < 3U) { legal = false; break; }

                        std::reverse(reversed_operations.begin(), reversed_operations.end());
                        std::reverse(reversed_right_sources.begin(), reversed_right_sources.end());
                        std::vector<VirtualRegister> lane_sources;
                        lane_sources.reserve(reversed_right_sources.size() + 1U);
                        lane_sources.push_back(first_source);
                        lane_sources.insert(lane_sources.end(), reversed_right_sources.begin(), reversed_right_sources.end());
                        if (lane == 0U) {
                            expected_operations = reversed_operations;
                            expected_sources = lane_sources;
                        } else if (expected_operations != reversed_operations || expected_sources != lane_sources) {
                            legal = false; break;
                        }
                        local_erase.insert(local_erase.end(), lane_erase.begin(), lane_erase.end());
                        local_results.insert(local_results.end(), lane_results.begin(), lane_results.end());
                    }
                    if (!legal || expected_operations.size() < 3U || destination == function.register_count) continue;
                    if (!slp_profitable({bytes, lanes, expected_operations.size(), expected_sources.size(), 1U, 3U,
                                         expected_operations.empty() ? Opcode::add_i64 : expected_operations.front(),
                                         SlpMemoryPattern::contiguous_unaligned, 0U, 0U,
                                         [&] {
                                             if (expected_operations.empty()) return 1.0;
                                             const auto base = slp_operation_multiplier(expected_operations.front(), slp_cost_model);
                                             double sum = 0.0;
                                             for (const auto op : expected_operations) sum += slp_operation_multiplier(op, slp_cost_model);
                                             return sum / (static_cast<double>(expected_operations.size()) * base);
                                         }()}, slp_cost_model, stats)) continue;

                    Instruction packed;
                    packed.vector_bits = slp_selected_vector_bits(bytes, lanes, slp_cost_model);
                    packed.opcode = bytes == 4U ? Opcode::binary_i32_contiguous_chain : Opcode::binary_i64_contiguous_chain;
                    packed.inputs = expected_sources;
                    packed.inputs.push_back(destination);
                    packed.immediate = static_cast<std::int64_t>(lanes);
                    packed.symbol.clear();
                    packed.symbol.reserve(expected_operations.size() * 2U);
                    for (const auto operation : expected_operations) {
                        const auto encoded = static_cast<std::uint32_t>(operation);
                        packed.symbol.push_back(static_cast<char>(encoded & 0xffU));
                        packed.symbol.push_back(static_cast<char>((encoded >> 8U) & 0xffU));
                    }
                    instructions[start] = std::move(packed);
                    for (std::size_t lane = 1U; lane < lanes; ++lane) erase_instructions.insert(&instructions[start + lane]);
                    for (auto* instruction : local_erase) erase_instructions.insert(instruction);
                    for (const auto value : local_results) erase_results.insert(value);
                    break;
                }
            }
        }
        if (!erase_instructions.empty()) {
            for (auto& block : function.blocks) {
                auto& instructions = block.instructions;
                instructions.erase(std::remove_if(instructions.begin(), instructions.end(), [&](const Instruction& instruction) {
                    return erase_instructions.contains(const_cast<Instruction*>(&instruction)) ||
                           (has_result(instruction.opcode) && erase_results.contains(instruction.result));
                }), instructions.end());
            }
        }
    }

    // Recognize two-stage vector expression chains over three contiguous
    // source arrays and one destination array:
    //
    //     dst[i] = (a[i] op1 b[i]) op2 c[i]
    //
    // The intermediate is kept entirely in the packed register domain. Both
    // operations are ordered lane-wise ADD/SUB/AND/OR/XOR for i32/i64.
    // Every scalar load/intermediate/final result must be single-use and the
    // destination stores must form one exact contiguous group.
    {
        std::vector<Instruction*> defs(function.register_count, nullptr);
        std::vector<std::uint32_t> uses(function.register_count, 0U);
        struct LoadInfo { bool valid{}; VirtualRegister base{}; std::int64_t offset{}; Opcode opcode{}; };
        std::vector<LoadInfo> loads(function.register_count);
        for (auto& b : function.blocks) for (auto& x : b.instructions) {
            if (has_result(x.opcode) && x.result < defs.size()) defs[x.result] = &x;
            for (auto r : x.inputs) if (r < uses.size()) ++uses[r];
            if ((x.opcode == Opcode::load_ptr_i32 || x.opcode == Opcode::load_ptr_i64) && x.result < loads.size() && x.inputs.size()==1U)
                loads[x.result] = {true, x.inputs[0], x.immediate, x.opcode};
        }
        const auto lane_width=[](Opcode op)->std::size_t {
            switch(op){case Opcode::add_i32:case Opcode::sub_i32:case Opcode::and_i32:case Opcode::or_i32:case Opcode::xor_i32:return 4U;
            case Opcode::add_i64:case Opcode::sub_i64:case Opcode::and_i64:case Opcode::or_i64:case Opcode::xor_i64:return 8U;default:return 0U;}
        };
        std::unordered_set<Instruction*> erase_ins;
        std::unordered_set<VirtualRegister> erase_results;
        for (auto& b : function.blocks) {
            auto& v=b.instructions;
            for(std::size_t start=0; start<v.size(); ++start){
                for(std::size_t lanes: {16U,8U,4U,2U}){
                    if(start+lanes>v.size()) continue;
                    Opcode storeop=v[start].opcode;
                    if(storeop!=Opcode::store_ptr_i32 && storeop!=Opcode::store_ptr_i64) continue;
                    const std::size_t bytes=storeop==Opcode::store_ptr_i32?4U:8U;
                    const Opcode loadop=bytes==4U?Opcode::load_ptr_i32:Opcode::load_ptr_i64;
                    VirtualRegister abase=function.register_count,bbase=function.register_count,cbase=function.register_count,dst=function.register_count;
                    Opcode op1=Opcode::jump,op2=Opcode::jump; bool legal=true;
                    std::vector<Instruction*> local_erase;
                    std::vector<VirtualRegister> local_results;
                    auto decode_load=[&](Instruction* op, std::size_t input_index, std::int64_t off, VirtualRegister& base, std::vector<Instruction*>& ei, std::vector<VirtualRegister>& er)->bool{
                        if(input_index>=op->inputs.size()) return false;
                        auto r=op->inputs[input_index];
                        if(r>=loads.size()||!loads[r].valid||loads[r].opcode!=loadop||loads[r].offset!=off||uses[r]!=1U) return false;
                        if(base==function.register_count) base=loads[r].base; else if(base!=loads[r].base) return false;
                        if (defs[r]) ei.push_back(defs[r]);
                        er.push_back(r);
                        return true;
                    };
                    for(std::size_t lane=0; lane<lanes && legal; ++lane){
                        auto& st=v[start+lane]; const auto off=static_cast<std::int64_t>(lane*bytes);
                        if(st.opcode!=storeop||st.inputs.size()!=2U||st.immediate!=off){legal=false;break;}
                        if(dst==function.register_count) dst=st.inputs[1]; else if(dst!=st.inputs[1]){legal=false;break;}
                        auto finalr=st.inputs[0]; if(finalr>=defs.size()||!defs[finalr]||uses[finalr]!=1U){legal=false;break;}
                        auto* second=defs[finalr]; auto w2=lane_width(second->opcode); if(w2!=bytes||second->inputs.size()!=2U){legal=false;break;}
                        if(lane==0)op2=second->opcode; else if(op2!=second->opcode){legal=false;break;}
                        Instruction* first=nullptr;
                        // Prefer the non-memory first operand as the intermediate.
                        auto mid=second->inputs[0];
                        if(mid<defs.size()) first=defs[mid];
                        if(!first||lane_width(first->opcode)!=bytes||uses[mid]!=1U){legal=false;break;}
                        if(lane==0)op1=first->opcode; else if(op1!=first->opcode){legal=false;break;}
                        if(first->inputs.size()!=2U){legal=false;break;}
                        // op1: A plus B, allowing B to have been folded as $memptr.
                        if(!decode_load(first,0,off,abase,local_erase,local_results)){legal=false;break;}
                        if(first->symbol=="$memptr") { if(first->immediate!=off){legal=false;break;} auto bb=first->inputs[1]; if(bbase==function.register_count)bbase=bb; else if(bbase!=bb){legal=false;break;} }
                        else if(!decode_load(first,1,off,bbase,local_erase,local_results)){legal=false;break;}
                        // op2: intermediate plus C, allowing C as $memptr.
                        if(second->symbol=="$memptr") { if(second->immediate!=off){legal=false;break;} auto cb=second->inputs[1]; if(cbase==function.register_count)cbase=cb; else if(cbase!=cb){legal=false;break;} }
                        else if(!decode_load(second,1,off,cbase,local_erase,local_results)){legal=false;break;}
                        local_erase.push_back(first); local_erase.push_back(second); local_results.push_back(mid); local_results.push_back(finalr);
                    }
                    if(!legal||abase==function.register_count||bbase==function.register_count||cbase==function.register_count||dst==function.register_count) continue;
                    if (!slp_profitable({bytes, lanes, 2U, 3U, 1U, 4U, op1,
                                         SlpMemoryPattern::contiguous_unaligned, 0U, 0U,
                                         (slp_operation_multiplier(op1, slp_cost_model) + slp_operation_multiplier(op2, slp_cost_model)) /
                                             (2.0 * slp_operation_multiplier(op1, slp_cost_model))}, slp_cost_model, stats)) continue;
                    Instruction packed; packed.opcode=bytes==4U?Opcode::binary_i32_contiguous_map3:Opcode::binary_i64_contiguous_map3;
                    packed.vector_bits = slp_selected_vector_bits(bytes, lanes, slp_cost_model);
                    packed.inputs={abase,bbase,cbase,dst}; packed.immediate=static_cast<std::int64_t>(lanes);
                    packed.argument_index=(static_cast<std::uint32_t>(op1)&0xffffU)|((static_cast<std::uint32_t>(op2)&0xffffU)<<16U);
                    v[start]=std::move(packed);
                    for(std::size_t lane=1;lane<lanes;++lane) erase_ins.insert(&v[start+lane]);
                    for (auto* x : local_erase) erase_ins.insert(x);
                    for (auto r : local_results) erase_results.insert(r);
                    break;
                }
            }
        }
        if(!erase_ins.empty()) for(auto& b:function.blocks){ auto& v=b.instructions; v.erase(std::remove_if(v.begin(),v.end(),[&](Instruction const& x){return erase_ins.contains(const_cast<Instruction*>(&x)) || (has_result(x.opcode)&&erase_results.contains(x.result));}),v.end()); }
    }

    // Recognize vector-to-vector integer SLP maps over two contiguous
    // source arrays and one contiguous destination array:
    //
    //     dst[i] = lhs[i] op rhs[i]
    //
    // The operation is lane-wise ADD/SUB/AND/OR/XOR for i32/i64. Every
    // scalar load and arithmetic result must be single-use, offsets must be
    // exact and contiguous, and arithmetic/store groups must be adjacent.
    // This preserves scalar alias/ordering semantics while allowing the x86
    // backend to issue true vector-to-vector SSE2 operations.
    {
        std::vector<std::uint32_t> uses(function.register_count, 0U);
        struct ScalarLoadInfo { bool valid{}; VirtualRegister base{}; std::int64_t offset{}; Opcode opcode{}; };
        std::vector<ScalarLoadInfo> scalar_loads(function.register_count);
        for (const auto& block : function.blocks) {
            for (const auto& ins : block.instructions) {
                for (const auto input : ins.inputs) if (input < uses.size()) ++uses[input];
                if ((ins.opcode == Opcode::load_ptr_i32 || ins.opcode == Opcode::load_ptr_i64) &&
                    ins.result < scalar_loads.size() && ins.inputs.size() == 1U)
                    scalar_loads[ins.result] = {true, ins.inputs[0], ins.immediate, ins.opcode};
            }
        }
        std::unordered_set<VirtualRegister> erased_loads;
        const auto lane_width = [](Opcode opcode) -> std::size_t {
            switch (opcode) {
            case Opcode::add_i32: case Opcode::sub_i32: case Opcode::and_i32: case Opcode::or_i32: case Opcode::xor_i32: return 4U;
            case Opcode::add_i64: case Opcode::sub_i64: case Opcode::and_i64: case Opcode::or_i64: case Opcode::xor_i64: return 8U;
            default: return 0U;
            }
        };
        const auto matching_store = [](Opcode arithmetic) -> Opcode {
            switch (arithmetic) {
            case Opcode::add_i32: case Opcode::sub_i32: case Opcode::and_i32: case Opcode::or_i32: case Opcode::xor_i32: return Opcode::store_ptr_i32;
            case Opcode::add_i64: case Opcode::sub_i64: case Opcode::and_i64: case Opcode::or_i64: case Opcode::xor_i64: return Opcode::store_ptr_i64;
            default: return Opcode::jump;
            }
        };
        for (auto& block : function.blocks) {
            auto& ins = block.instructions;
            for (std::size_t start = 0; start < ins.size(); ++start) {
                for (const std::size_t lanes : {16U, 8U, 4U, 2U}) {
                    if (start + lanes * 2U > ins.size()) continue;
                    const auto arithmetic_opcode = ins[start].opcode;
                    const auto bytes = lane_width(arithmetic_opcode);
                    if (bytes == 0U) continue;
                    const auto load_opcode = bytes == 4U ? Opcode::load_ptr_i32 : Opcode::load_ptr_i64;
                    const auto store_opcode = matching_store(arithmetic_opcode);
                    VirtualRegister lhs_base = function.register_count;
                    VirtualRegister rhs_base = function.register_count;
                    VirtualRegister dst_base = function.register_count;
                    bool legal = true;
                    std::vector<VirtualRegister> candidate_loads;
                    for (std::size_t lane = 0; lane < lanes; ++lane) {
                        const auto& arithmetic = ins[start + lane];
                        const auto& store = ins[start + lanes + lane];
                        if (arithmetic.opcode != arithmetic_opcode || arithmetic.inputs.size() != 2U ||
                            store.opcode != store_opcode || store.inputs.size() != 2U ||
                            store.inputs[0] != arithmetic.result || arithmetic.result >= uses.size() ||
                            uses[arithmetic.result] != 1U ||
                            store.immediate != static_cast<std::int64_t>(lane * bytes)) {
                            legal = false; break;
                        }
                        VirtualRegister lane_lhs_base = function.register_count;
                        VirtualRegister lane_rhs_base = function.register_count;
                        VirtualRegister lhs_load = function.register_count;
                        VirtualRegister rhs_load = function.register_count;
                        const auto expected_offset = static_cast<std::int64_t>(lane * bytes);
                        if (arithmetic.symbol == "$memptr") {
                            // Existing scalar memory folding leaves the first
                            // array as a scalar load and encodes the second
                            // directly as [base+offset].
                            lhs_load = arithmetic.inputs[0];
                            if (lhs_load >= scalar_loads.size() || !scalar_loads[lhs_load].valid ||
                                scalar_loads[lhs_load].opcode != load_opcode || uses[lhs_load] != 1U ||
                                scalar_loads[lhs_load].offset != expected_offset || arithmetic.immediate != expected_offset) {
                                legal = false; break;
                            }
                            lane_lhs_base = scalar_loads[lhs_load].base;
                            lane_rhs_base = arithmetic.inputs[1];
                        } else if (arithmetic.symbol.empty()) {
                            lhs_load = arithmetic.inputs[0];
                            rhs_load = arithmetic.inputs[1];
                            if (lhs_load >= scalar_loads.size() || rhs_load >= scalar_loads.size() ||
                                !scalar_loads[lhs_load].valid || !scalar_loads[rhs_load].valid ||
                                scalar_loads[lhs_load].opcode != load_opcode || scalar_loads[rhs_load].opcode != load_opcode ||
                                uses[lhs_load] != 1U || uses[rhs_load] != 1U ||
                                scalar_loads[lhs_load].offset != expected_offset || scalar_loads[rhs_load].offset != expected_offset) {
                                legal = false; break;
                            }
                            lane_lhs_base = scalar_loads[lhs_load].base;
                            lane_rhs_base = scalar_loads[rhs_load].base;
                        } else {
                            legal = false; break;
                        }
                        if (lane == 0U) {
                            lhs_base = lane_lhs_base;
                            rhs_base = lane_rhs_base;
                            dst_base = store.inputs[1];
                        } else if (lhs_base != lane_lhs_base || rhs_base != lane_rhs_base || dst_base != store.inputs[1]) {
                            legal = false; break;
                        }
                        candidate_loads.push_back(lhs_load);
                        if (rhs_load < function.register_count) candidate_loads.push_back(rhs_load);
                    }
                    if (!legal || lhs_base == function.register_count || rhs_base == function.register_count || dst_base == function.register_count)
                        continue;
                    if (!slp_profitable({bytes, lanes, 1U, 2U, 1U, 3U, arithmetic_opcode,
                                         SlpMemoryPattern::contiguous_unaligned, 0U, 0U, 1.0}, slp_cost_model, stats)) continue;
                    erased_loads.insert(candidate_loads.begin(), candidate_loads.end());
                    Instruction packed;
                    packed.vector_bits = slp_selected_vector_bits(bytes, lanes, slp_cost_model);
                    packed.opcode = bytes == 4U ? Opcode::binary_i32_contiguous_map2 : Opcode::binary_i64_contiguous_map2;
                    packed.inputs = {lhs_base, rhs_base, dst_base};
                    packed.immediate = static_cast<std::int64_t>(lanes);
                    packed.argument_index = static_cast<std::uint32_t>(arithmetic_opcode);
                    ins[start] = std::move(packed);
                    ins.erase(ins.begin() + static_cast<std::ptrdiff_t>(start + 1U),
                              ins.begin() + static_cast<std::ptrdiff_t>(start + lanes * 2U));
                    break;
                }
            }
        }
        if (!erased_loads.empty()) {
            for (auto& block : function.blocks) {
                auto& instructions = block.instructions;
                instructions.erase(std::remove_if(instructions.begin(), instructions.end(), [&](const Instruction& ins) {
                    return has_result(ins.opcode) && erased_loads.contains(ins.result);
                }), instructions.end());
            }
        }
    }

    // Recognize straight-line i32 addition reductions over contiguous
    // 4-byte loads. Like the i64 SLP path below, this is legality-first and
    // only fires for exact single-use reduction trees with one common base.
    {
        std::vector<Instruction*> defs(function.register_count, nullptr);
        std::vector<std::uint32_t> uses(function.register_count, 0U);
        for (auto& block : function.blocks) {
            for (auto& ins : block.instructions) {
                if (has_result(ins.opcode) && ins.result < defs.size()) defs[ins.result] = &ins;
                for (auto input : ins.inputs) if (input < uses.size()) ++uses[input];
            }
        }

        struct ReductionLeaf { VirtualRegister base{}; std::int64_t offset{}; };
        std::unordered_set<VirtualRegister> slp_erased_results;
        for (auto& block : function.blocks) {
            for (auto& ret : block.instructions) {
                if (ret.opcode != Opcode::return_i32 || ret.inputs.size() != 1U) continue;
                const auto root = ret.inputs.front();
                std::vector<ReductionLeaf> leaves;
                std::vector<VirtualRegister> visited;
                const auto collect = [&](auto&& self, VirtualRegister reg) -> bool {
                    if (reg >= defs.size() || defs[reg] == nullptr) return false;
                    auto* def = defs[reg];
                    if (def->opcode == Opcode::load_ptr_i32 && def->inputs.size() == 1U) {
                        leaves.push_back({def->inputs[0], def->immediate});
                        visited.push_back(reg);
                        return true;
                    }
                    if (def->opcode != Opcode::add_i32 || uses[reg] != 1U) return false;
                    if (def->symbol == "$memptr") {
                        if (def->inputs.size() != 2U) return false;
                        if (!self(self, def->inputs[0])) return false;
                        leaves.push_back({def->inputs[1], def->immediate});
                        visited.push_back(reg);
                        return true;
                    }
                    if (!def->symbol.empty() || def->inputs.size() != 2U) return false;
                    if (!self(self, def->inputs[0]) || !self(self, def->inputs[1])) return false;
                    visited.push_back(reg);
                    return true;
                };
                if (!collect(collect, root)) continue;
                if (leaves.size() < 4U || leaves.size() > 32U || (leaves.size() & (leaves.size() - 1U)) != 0U) continue;
                const auto base = leaves.front().base;
                bool legal = true;
                std::vector<std::int64_t> offsets; offsets.reserve(leaves.size());
                for (const auto& leaf : leaves) {
                    if (leaf.base != base) { legal = false; break; }
                    offsets.push_back(leaf.offset);
                }
                if (!legal) continue;
                std::sort(offsets.begin(), offsets.end());
                for (std::size_t i = 0; i < offsets.size(); ++i)
                    if (offsets[i] != static_cast<std::int64_t>(i * 4U)) { legal = false; break; }
                if (!legal) continue;

                auto* root_def = defs[root];
                root_def->opcode = Opcode::reduce_add_i32_contiguous;
                root_def->inputs = {base};
                root_def->immediate = static_cast<std::int64_t>(leaves.size());
                root_def->symbol.clear();
                for (const auto reg : visited) if (reg != root) slp_erased_results.insert(reg);
                break;
            }
        }
        if (!slp_erased_results.empty()) {
            for (auto& block : function.blocks) {
                auto& instructions = block.instructions;
                instructions.erase(std::remove_if(instructions.begin(), instructions.end(), [&](const Instruction& ins) {
                    return has_result(ins.opcode) && slp_erased_results.contains(ins.result);
                }), instructions.end());
            }
        }
    }

    // Recognize a straight-line i64 addition reduction whose leaves are
    // contiguous 8-byte loads from the same base pointer. Represent it as a
    // single machine pseudo-op so the target encoder can use packed SSE2
    // loads/adds without introducing vector types into the public Forge IR.
    // This is deliberately legality-first: all reduction nodes must be
    // single-use, every leaf must be a plain pointer load, and offsets must
    // form an exact 0,8,... sequence.
    {
        std::vector<Instruction*> defs(function.register_count, nullptr);
        std::vector<std::uint32_t> uses(function.register_count, 0U);
        for (auto& block : function.blocks) {
            for (auto& ins : block.instructions) {
                if (has_result(ins.opcode) && ins.result < defs.size()) defs[ins.result] = &ins;
                for (auto input : ins.inputs) if (input < uses.size()) ++uses[input];
            }
        }

        struct ReductionLeaf { VirtualRegister base{}; std::int64_t offset{}; };
        std::unordered_set<VirtualRegister> slp_erased_results;
        for (auto& block : function.blocks) {
            for (auto& ret : block.instructions) {
                if (ret.opcode != Opcode::return_i64 || ret.inputs.size() != 1U) continue;
                const auto root = ret.inputs.front();
                std::vector<ReductionLeaf> leaves;
                std::vector<VirtualRegister> visited;
                const auto collect = [&](auto&& self, VirtualRegister reg) -> bool {
                    if (reg >= defs.size() || defs[reg] == nullptr) return false;
                    auto* def = defs[reg];
                    if (def->opcode == Opcode::load_ptr_i64 && def->inputs.size() == 1U) {
                        leaves.push_back({def->inputs[0], def->immediate});
                        visited.push_back(reg);
                        return true;
                    }
                    if (def->opcode != Opcode::add_i64 || uses[reg] != 1U) return false;
                    if (def->symbol == "$memptr") {
                        if (def->inputs.size() != 2U) return false;
                        if (!self(self, def->inputs[0])) return false;
                        leaves.push_back({def->inputs[1], def->immediate});
                        visited.push_back(reg);
                        return true;
                    }
                    if (!def->symbol.empty() || def->inputs.size() != 2U) return false;
                    if (!self(self, def->inputs[0]) || !self(self, def->inputs[1])) return false;
                    visited.push_back(reg);
                    return true;
                };
                if (!collect(collect, root)) continue;
                if (leaves.size() < 4U || leaves.size() > 16U || (leaves.size() & (leaves.size() - 1U)) != 0U) continue;
                const auto base = leaves.front().base;
                bool legal = true;
                std::vector<std::int64_t> offsets; offsets.reserve(leaves.size());
                for (const auto& leaf : leaves) {
                    if (leaf.base != base) { legal = false; break; }
                    offsets.push_back(leaf.offset);
                }
                if (!legal) continue;
                std::sort(offsets.begin(), offsets.end());
                for (std::size_t i = 0; i < offsets.size(); ++i)
                    if (offsets[i] != static_cast<std::int64_t>(i * 8U)) { legal = false; break; }
                if (!legal) continue;

                auto* root_def = defs[root];
                root_def->opcode = Opcode::reduce_add_i64_contiguous;
                root_def->inputs = {base};
                root_def->immediate = static_cast<std::int64_t>(leaves.size());
                root_def->symbol.clear();
                for (const auto reg : visited) if (reg != root) slp_erased_results.insert(reg);
                break;
            }
        }
        if (!slp_erased_results.empty()) {
            for (auto& block : function.blocks) {
                auto& instructions = block.instructions;
                instructions.erase(std::remove_if(instructions.begin(), instructions.end(), [&](const Instruction& ins) {
                    return has_result(ins.opcode) && slp_erased_results.contains(ins.result);
                }), instructions.end());
            }
        }
    }

    // Global machine dead-code elimination. Machine IR is SSA here, so a
    // result is globally dead exactly when it has no remaining uses. Drive
    // deletion from a def/use worklist and decrement producer use counts as
    // consumers disappear. This reaches the same transitive fixed point across
    // block boundaries without rebuilding whole-function liveness after every
    // sweep.
    const auto no_definition = std::numeric_limits<std::size_t>::max();
    std::vector<std::uint32_t> use_count(function.register_count, 0U);
    std::vector<std::size_t> definition_block(function.register_count, no_definition);
    std::vector<std::size_t> definition_instruction(function.register_count, no_definition);
    std::vector<std::vector<bool>> removed(function.blocks.size());

    for (std::size_t block_index = 0; block_index < function.blocks.size(); ++block_index) {
        const auto& instructions = function.blocks[block_index].instructions;
        removed[block_index].assign(instructions.size(), false);
        for (std::size_t instruction_index = 0; instruction_index < instructions.size(); ++instruction_index) {
            const auto& instruction = instructions[instruction_index];
            for (const auto input : instruction.inputs)
                if (input < use_count.size()) ++use_count[input];
            for (const auto& successor : instruction.successors)
                for (const auto argument : successor.arguments)
                    if (argument < use_count.size()) ++use_count[argument];
            if (has_result(instruction.opcode) && instruction.result < function.register_count) {
                definition_block[instruction.result] = block_index;
                definition_instruction[instruction.result] = instruction_index;
            }
        }
    }

    std::vector<VirtualRegister> dead_worklist;
    dead_worklist.reserve(function.register_count / 4U + 1U);
    const auto enqueue_if_dead = [&](VirtualRegister reg) {
        if (reg >= function.register_count || use_count[reg] != 0U) return;
        const auto block_index = definition_block[reg];
        const auto instruction_index = definition_instruction[reg];
        if (block_index == no_definition || instruction_index == no_definition) return;
        if (is_removable_when_dead(function.blocks[block_index].instructions[instruction_index].opcode))
            dead_worklist.push_back(reg);
    };
    for (VirtualRegister reg = 0; reg < function.register_count; ++reg) enqueue_if_dead(reg);

    while (!dead_worklist.empty()) {
        const auto reg = dead_worklist.back();
        dead_worklist.pop_back();
        if (reg >= function.register_count || use_count[reg] != 0U) continue;
        const auto block_index = definition_block[reg];
        const auto instruction_index = definition_instruction[reg];
        if (block_index == no_definition || instruction_index == no_definition ||
            removed[block_index][instruction_index]) continue;
        const auto& instruction = function.blocks[block_index].instructions[instruction_index];
        if (!is_removable_when_dead(instruction.opcode)) continue;

        removed[block_index][instruction_index] = true;
        ++stats.dead_instructions_eliminated;
        if (is_comparison(instruction.opcode)) ++stats.dead_comparisons_eliminated;
        const auto release_use = [&](VirtualRegister input) {
            if (input >= use_count.size() || use_count[input] == 0U) return;
            if (--use_count[input] == 0U) enqueue_if_dead(input);
        };
        for (const auto input : instruction.inputs) release_use(input);
        for (const auto& successor : instruction.successors)
            for (const auto argument : successor.arguments) release_use(argument);
    }

    for (std::size_t block_index = 0; block_index < function.blocks.size(); ++block_index) {
        auto& instructions = function.blocks[block_index].instructions;
        std::vector<Instruction> retained;
        retained.reserve(instructions.size());
        for (std::size_t instruction_index = 0; instruction_index < instructions.size(); ++instruction_index)
            if (!removed[block_index][instruction_index]) retained.push_back(std::move(instructions[instruction_index]));
        instructions = std::move(retained);
    }

    // Keep the existing liveness quality counters, but materialize boundary
    // liveness only once after DCE. Instruction-local live-after state is not
    // needed by the worklist algorithm.
    const auto post_dce_liveness = analyze_liveness(function, false);
    stats.liveness_iterations += post_dce_liveness.fixed_point_iterations;
    stats.cross_block_live_values = std::max(stats.cross_block_live_values,
                                              post_dce_liveness.cross_block_live_values);

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

OptimizationStats optimize_module(Module& module, const SlpCostModel& slp_cost_model) {
    OptimizationStats total;
    for (auto& function : module.functions) {
        const auto stats = optimize_function(function, slp_cost_model);
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
