// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/machine/verifier.hpp"

#include <algorithm>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace forge::machine {
namespace {
void error(Diagnostics& out, std::string message) {
    out.push_back({DiagnosticSeverity::error, std::move(message), {}});
}

bool has_result(Opcode opcode) {
    return opcode != Opcode::jump && opcode != Opcode::branch_i1 && opcode != Opcode::return_i32 && opcode != Opcode::return_i64 && opcode != Opcode::return_f32 && opcode != Opcode::return_f64 && opcode != Opcode::return_void && opcode != Opcode::return_aggregate && opcode != Opcode::call_void && opcode != Opcode::call_aggregate && opcode != Opcode::call_indirect_void && opcode != Opcode::store_stack_i8 && opcode != Opcode::store_stack_i16 && opcode != Opcode::store_stack_i32 && opcode != Opcode::store_stack_i64 && opcode != Opcode::store_stack_f32 && opcode != Opcode::store_stack_f64 && opcode != Opcode::store_ptr_i8 && opcode != Opcode::store_ptr_i16 && opcode != Opcode::store_ptr_i32 && opcode != Opcode::store_ptr_i64 && opcode != Opcode::store_ptr_f32 && opcode != Opcode::store_ptr_f64 && opcode != Opcode::store_stack_v128 && opcode != Opcode::store_stack_v256 && opcode != Opcode::store_stack_v512 && opcode != Opcode::add_i64_contiguous_inplace && opcode != Opcode::binary_i32_contiguous_inplace && opcode != Opcode::binary_i64_contiguous_inplace && opcode != Opcode::binary_i32_contiguous_map && opcode != Opcode::binary_i64_contiguous_map && opcode != Opcode::binary_i32_contiguous_map2 && opcode != Opcode::binary_i64_contiguous_map2 && opcode != Opcode::binary_i32_contiguous_map3 && opcode != Opcode::binary_i64_contiguous_map3 && opcode != Opcode::binary_i32_contiguous_chain && opcode != Opcode::binary_i64_contiguous_chain && opcode != Opcode::binary_i32_contiguous_dag && opcode != Opcode::binary_i64_contiguous_dag && opcode != Opcode::binary_i32_contiguous_dag_reuse && opcode != Opcode::binary_i64_contiguous_dag_reuse;
}

bool is_float_arithmetic(Opcode opcode) {
    switch (opcode) {
    case Opcode::load_immediate_f32: case Opcode::load_immediate_f64:
    case Opcode::load_argument_f32: case Opcode::load_argument_f64:
    case Opcode::load_stack_f32: case Opcode::load_stack_f64:
    case Opcode::load_ptr_f32: case Opcode::load_ptr_f64:
    case Opcode::copy_f32: case Opcode::copy_f64:
    case Opcode::add_f32: case Opcode::add_f64:
    case Opcode::sub_f32: case Opcode::sub_f64:
    case Opcode::mul_f32: case Opcode::mul_f64:
    case Opcode::div_f32: case Opcode::div_f64:
    case Opcode::neg_f32: case Opcode::neg_f64:
    case Opcode::call_f32: case Opcode::call_f64:
    case Opcode::call_indirect_f32: case Opcode::call_indirect_f64:
    case Opcode::int_to_float_signed: case Opcode::int_to_float_unsigned:
    case Opcode::float_extend: case Opcode::float_truncate:
        return true;
    default:
        return false;
    }
}

bool is_float_comparison(Opcode opcode) {
    return opcode >= Opcode::cmp_eq_f32 && opcode <= Opcode::cmp_ge_f64;
}

bool is_integer_comparison(Opcode opcode) {
    return opcode >= Opcode::cmp_eq_i32 && opcode <= Opcode::cmp_uge_i64;
}

bool requires_float_inputs(Opcode opcode) {
    return is_float_comparison(opcode) ||
           opcode == Opcode::copy_f32 || opcode == Opcode::copy_f64 ||
           opcode == Opcode::add_f32 || opcode == Opcode::add_f64 ||
           opcode == Opcode::sub_f32 || opcode == Opcode::sub_f64 ||
           opcode == Opcode::mul_f32 || opcode == Opcode::mul_f64 ||
           opcode == Opcode::div_f32 || opcode == Opcode::div_f64 ||
           opcode == Opcode::neg_f32 || opcode == Opcode::neg_f64 ||
           opcode == Opcode::store_stack_f32 || opcode == Opcode::store_stack_f64 ||
           opcode == Opcode::store_ptr_f32 || opcode == Opcode::store_ptr_f64 ||
           opcode == Opcode::return_f32 || opcode == Opcode::return_f64 ||
           opcode == Opcode::float_to_int_signed || opcode == Opcode::float_to_int_unsigned ||
           opcode == Opcode::float_extend || opcode == Opcode::float_truncate;
}

bool supports_integer_immediate(Opcode opcode) {
    switch (opcode) {
    case Opcode::add_i32: case Opcode::add_i64: case Opcode::sub_i32: case Opcode::sub_i64:
    case Opcode::mul_i32: case Opcode::mul_i64: case Opcode::and_i32: case Opcode::and_i64:
    case Opcode::or_i32: case Opcode::or_i64: case Opcode::xor_i32: case Opcode::xor_i64:
    case Opcode::shl_i32: case Opcode::shl_i64: case Opcode::shr_s_i32: case Opcode::shr_s_i64:
    case Opcode::shr_u_i32: case Opcode::shr_u_i64: return true;
    default: return false;
    }
}

bool is_terminator(Opcode opcode) {
    return opcode == Opcode::jump || opcode == Opcode::branch_i1 || opcode == Opcode::return_i32 || opcode == Opcode::return_i64 || opcode == Opcode::return_f32 || opcode == Opcode::return_f64 || opcode == Opcode::return_void || opcode == Opcode::return_aggregate;
}

// The packed binary forms occupy one contiguous run of the opcode enum, so a
// range test keeps this in step with the enum rather than a list that can rot.
bool is_packed_binary(Opcode opcode) {
    return opcode >= Opcode::binary_i32_contiguous_inplace &&
           opcode <= Opcode::binary_i64_contiguous_dag_reuse;
}

bool packed_is_wide(Opcode opcode) {
    switch (opcode) {
    case Opcode::binary_i64_contiguous_inplace:
    case Opcode::binary_i64_contiguous_map:
    case Opcode::binary_i64_contiguous_map2:
    case Opcode::binary_i64_contiguous_map3:
    case Opcode::binary_i64_contiguous_chain:
    case Opcode::binary_i64_contiguous_dag:
    case Opcode::binary_i64_contiguous_dag_reuse: return true;
    default: return false;
    }
}

std::uint16_t packed_token(const std::string& program, std::size_t offset) {
    return static_cast<std::uint16_t>(
        static_cast<unsigned char>(program[offset]) |
        (static_cast<unsigned>(static_cast<unsigned char>(program[offset + 1U])) << 8U));
}

// Only lane-wise operations with a direct packed encoding are admissible;
// anything else would have no legal vector form to lower to. The packed
// pseudo names retain their historical i32/i64 spelling because they describe
// lane width, not the arithmetic domain.
bool packed_operation_supported(Opcode operation, bool wide) {
    if (wide)
        return operation == Opcode::add_i64 || operation == Opcode::sub_i64 ||
               operation == Opcode::and_i64 || operation == Opcode::or_i64 ||
               operation == Opcode::xor_i64 || operation == Opcode::add_f64 ||
               operation == Opcode::sub_f64 || operation == Opcode::mul_f64 ||
               operation == Opcode::div_f64;
    return operation == Opcode::add_i32 || operation == Opcode::sub_i32 ||
           operation == Opcode::and_i32 || operation == Opcode::or_i32 ||
           operation == Opcode::xor_i32 || operation == Opcode::add_f32 ||
           operation == Opcode::sub_f32 || operation == Opcode::mul_f32 ||
           operation == Opcode::div_f32;
}

bool packed_operation_is_float(Opcode operation, bool wide) {
    if (wide)
        return operation == Opcode::add_f64 || operation == Opcode::sub_f64 ||
               operation == Opcode::mul_f64 || operation == Opcode::div_f64;
    return operation == Opcode::add_f32 || operation == Opcode::sub_f32 ||
           operation == Opcode::mul_f32 || operation == Opcode::div_f32;
}

void verify_packed_binary(const Instruction& ins, const std::string& name, Diagnostics& diagnostics) {
    const bool wide = packed_is_wide(ins.opcode);
    const bool chained = ins.opcode == Opcode::binary_i32_contiguous_map3 ||
                         ins.opcode == Opcode::binary_i64_contiguous_map3;
    const bool arbitrary_chain = ins.opcode == Opcode::binary_i32_contiguous_chain ||
                                 ins.opcode == Opcode::binary_i64_contiguous_chain;
    const bool arbitrary_dag = ins.opcode == Opcode::binary_i32_contiguous_dag ||
                               ins.opcode == Opcode::binary_i64_contiguous_dag;
    const bool reusable_dag = ins.opcode == Opcode::binary_i32_contiguous_dag_reuse ||
                              ins.opcode == Opcode::binary_i64_contiguous_dag_reuse;

    if (ins.vector_mask_lanes != 0U) {
        const auto max_mask_lanes = static_cast<std::uint8_t>(wide ? 8U : 16U);
        if (ins.vector_bits != 512U || ins.vector_mask_lanes > max_mask_lanes)
            error(diagnostics, "packed AVX-512 lane mask requires a valid 512-bit pack in @" + name);
    }

    if (arbitrary_chain) {
        if (ins.symbol.size() < 6U || (ins.symbol.size() & 1U) != 0U) {
            error(diagnostics, "packed chain must contain at least three encoded operations in @" + name);
            return;
        }
        const auto operation_count = ins.symbol.size() / 2U;
        if (ins.inputs.size() != operation_count + 2U)
            error(diagnostics, "packed chain source/operation mismatch in @" + name);
        std::optional<bool> floating;
        for (std::size_t offset = 0; offset + 1U < ins.symbol.size(); offset += 2U) {
            const auto operation = static_cast<Opcode>(packed_token(ins.symbol, offset));
            if (!packed_operation_supported(operation, wide))
                error(diagnostics, "unsupported packed chain operation in @" + name);
            const auto operation_floating = packed_operation_is_float(operation, wide);
            if (floating && *floating != operation_floating)
                error(diagnostics, "packed chain cannot mix integer and floating operations in @" + name);
            floating = operation_floating;
        }
    }

    if (arbitrary_dag) {
        if (ins.symbol.size() < 10U || (ins.symbol.size() & 1U) != 0U) {
            error(diagnostics, "packed DAG must contain a valid postfix program in @" + name);
            return;
        }
        // Walk the postfix program as an abstract stack machine: a source token
        // pushes, an operation pops two and pushes one.
        std::size_t stack_depth = 0U;
        std::size_t max_stack_depth = 0U;
        std::size_t max_source = 0U;
        bool saw_source = false;
        std::optional<bool> floating;
        for (std::size_t offset = 0; offset + 1U < ins.symbol.size(); offset += 2U) {
            const auto token = packed_token(ins.symbol, offset);
            if ((token & 0x8000U) != 0U) {
                ++stack_depth;
                max_stack_depth = std::max(max_stack_depth, stack_depth);
                max_source = std::max(max_source, static_cast<std::size_t>(token & 0x7fffU));
                saw_source = true;
                continue;
            }
            const auto operation = static_cast<Opcode>(token);
            if (!packed_operation_supported(operation, wide))
                error(diagnostics, "unsupported packed DAG operation in @" + name);
            const auto operation_floating = packed_operation_is_float(operation, wide);
            if (floating && *floating != operation_floating)
                error(diagnostics, "packed DAG cannot mix integer and floating operations in @" + name);
            floating = operation_floating;
            if (stack_depth < 2U) error(diagnostics, "packed DAG postfix stack underflow in @" + name);
            else --stack_depth;
        }
        if (stack_depth != 1U)
            error(diagnostics, "packed DAG postfix stack must end at depth one in @" + name);
        // Every live intermediate needs its own vector register.
        if (max_stack_depth > 8U)
            error(diagnostics, "packed DAG exceeds supported vector evaluation depth in @" + name);
        const auto source_count = saw_source ? max_source + 1U : 0U;
        if (source_count == 0U || ins.inputs.size() != source_count + 1U)
            error(diagnostics, "packed DAG source/program mismatch in @" + name);
    }

    if (reusable_dag) {
        if (ins.symbol.size() < 18U || (ins.symbol.size() % 6U) != 0U) {
            error(diagnostics, "packed reusable DAG must contain fixed-size node records in @" + name);
            return;
        }
        // Fixed six-byte records: a tag, then two operand node indices.
        const auto node_count = ins.symbol.size() / 6U;
        std::size_t max_source = 0U;
        bool saw_source = false;
        std::optional<bool> floating;
        for (std::size_t node = 0; node < node_count; ++node) {
            const auto tag = packed_token(ins.symbol, node * 6U);
            const auto lhs = packed_token(ins.symbol, node * 6U + 2U);
            const auto rhs = packed_token(ins.symbol, node * 6U + 4U);
            if ((tag & 0x8000U) != 0U) {
                saw_source = true;
                max_source = std::max(max_source, static_cast<std::size_t>(tag & 0x7fffU));
                continue;
            }
            const auto operation = static_cast<Opcode>(tag);
            if (!packed_operation_supported(operation, wide))
                error(diagnostics, "unsupported packed reusable DAG operation in @" + name);
            const auto operation_floating = packed_operation_is_float(operation, wide);
            if (floating && *floating != operation_floating)
                error(diagnostics, "packed reusable DAG cannot mix integer and floating operations in @" + name);
            floating = operation_floating;
            // Referencing only earlier nodes keeps the graph acyclic, so it can
            // be evaluated in a single forward pass.
            if (lhs >= node || rhs >= node)
                error(diagnostics, "packed reusable DAG node must reference earlier nodes in @" + name);
        }
        const auto source_count = saw_source ? max_source + 1U : 0U;
        if (source_count == 0U || ins.inputs.size() != source_count + 1U)
            error(diagnostics, "packed reusable DAG source/program mismatch in @" + name);
    }

    if (!arbitrary_chain && !arbitrary_dag && !reusable_dag) {
        const auto operation = static_cast<Opcode>(chained ? (ins.argument_index & 0xffffU) : ins.argument_index);
        if (!packed_operation_supported(operation, wide))
            error(diagnostics, "unsupported packed operation in @" + name);
        if (chained) {
            const auto second = static_cast<Opcode>((ins.argument_index >> 16U) & 0xffffU);
            if (!packed_operation_supported(second, wide))
                error(diagnostics, "unsupported second packed operation in @" + name);
            if (packed_operation_is_float(operation, wide) != packed_operation_is_float(second, wide))
                error(diagnostics, "packed chained map cannot mix integer and floating operations in @" + name);
        }
    }

    if (ins.immediate < 2 || ins.immediate > 16) {
        error(diagnostics, "invalid packed lane count in @" + name);
        return;
    }

    // Packed maps/chains may end in a scalar lane or an 8-byte NEON D-register
    // tail, so requiring powers of two here incorrectly rejects profitable
    // 3/6/10/... lane groups. DAG encodings are evaluated chunk-wise and need
    // at least a complete 64-bit tail because they do not have a scalar postfix
    // evaluator. Keep that stricter invariant only for those two pseudo forms.
    if ((arbitrary_dag || reusable_dag) &&
        ((static_cast<std::uint64_t>(ins.immediate) * (wide ? 8U : 4U)) & 7U) != 0U)
        error(diagnostics, "packed DAG tail must cover at least 64 bits in @" + name);
}

std::size_t expected_inputs(Opcode opcode) {
    switch (opcode) {
    case Opcode::load_argument:
    case Opcode::load_argument_i64:
    case Opcode::load_argument_f32:
    case Opcode::load_argument_f64:
    case Opcode::load_immediate:
    case Opcode::load_immediate_i64:
    case Opcode::load_immediate_f32:
    case Opcode::load_immediate_f64:
    case Opcode::load_function_address:
    case Opcode::load_global_address:
    case Opcode::load_tls_address:
    case Opcode::load_stack_address:
    case Opcode::load_stack_i8:
    case Opcode::load_stack_i16:
    case Opcode::load_stack_i32:
    case Opcode::load_stack_i64:
    case Opcode::load_stack_f32:
    case Opcode::load_stack_f64:
    case Opcode::load_stack_v128:
    case Opcode::load_stack_v256:
    case Opcode::load_stack_v512: return 0;
    case Opcode::copy:
    case Opcode::copy_f32:
    case Opcode::copy_f64:
    case Opcode::neg_f32:
    case Opcode::neg_f64:
    case Opcode::neg_i32:
    case Opcode::neg_i64:
    case Opcode::not_i32:
    case Opcode::not_i64:
    case Opcode::zero_extend:
    case Opcode::sign_extend:
    case Opcode::truncate:
    case Opcode::int_to_float_signed:
    case Opcode::int_to_float_unsigned:
    case Opcode::float_to_int_signed:
    case Opcode::float_to_int_unsigned:
    case Opcode::float_extend:
    case Opcode::float_truncate:
    case Opcode::branch_i1:
    case Opcode::return_i32:
    case Opcode::return_i64:
    case Opcode::return_f32:
    case Opcode::return_f64:
    case Opcode::store_stack_i8:
    case Opcode::store_stack_i16:
    case Opcode::store_stack_i32:
    case Opcode::store_stack_i64:
    case Opcode::store_stack_f32:
    case Opcode::store_stack_f64:
    case Opcode::store_stack_v128:
    case Opcode::store_stack_v256:
    case Opcode::store_stack_v512:
    case Opcode::reduce_add_i32_contiguous:
    case Opcode::reduce_add_i64_contiguous:
    case Opcode::load_ptr_i8:
    case Opcode::load_ptr_i16:
    case Opcode::load_ptr_i32:
    case Opcode::load_ptr_i64:
    case Opcode::load_ptr_f32:
    case Opcode::load_ptr_f64:
    case Opcode::ptr_offset: return 1;
    case Opcode::store_ptr_i8:
    case Opcode::store_ptr_i16:
    case Opcode::store_ptr_i32:
    case Opcode::store_ptr_i64:
    case Opcode::store_ptr_f32:
    case Opcode::store_ptr_f64: return 2;
    case Opcode::add_i64_contiguous_inplace:
    case Opcode::binary_i32_contiguous_inplace:
    case Opcode::binary_i64_contiguous_inplace: return 2;
    case Opcode::binary_i32_contiguous_map:
    case Opcode::binary_i64_contiguous_map:
    case Opcode::binary_i32_contiguous_map2:
    case Opcode::binary_i64_contiguous_map2: return 3;
    case Opcode::binary_i32_contiguous_map3:
    case Opcode::binary_i64_contiguous_map3: return 4;
    // Chain and DAG forms carry a variable number of sources, checked below
    // against their encoded program rather than by a fixed count.
    case Opcode::binary_i32_contiguous_chain:
    case Opcode::binary_i64_contiguous_chain:
    case Opcode::binary_i32_contiguous_dag:
    case Opcode::binary_i64_contiguous_dag:
    case Opcode::binary_i32_contiguous_dag_reuse:
    case Opcode::binary_i64_contiguous_dag_reuse: return static_cast<std::size_t>(-1);
    case Opcode::select_i32:
    case Opcode::select_i64: return 3;
    case Opcode::return_void: return 0;
    case Opcode::return_aggregate: return 1;
    case Opcode::jump: return 0;
    case Opcode::call_i32:
    case Opcode::call_i64:
    case Opcode::call_f32:
    case Opcode::call_f64:
    case Opcode::call_void:
    case Opcode::call_aggregate:
    case Opcode::call_indirect_i32:
    case Opcode::call_indirect_i64:
    case Opcode::call_indirect_f32:
    case Opcode::call_indirect_f64:
    case Opcode::call_indirect_void: return static_cast<std::size_t>(-1);
    default: return 2;
    }
}
}

Diagnostics verify_module(const Module& module) {
    Diagnostics diagnostics;
    std::unordered_set<std::string> functions;
    for (const auto& function : module.functions) {
        if (!functions.insert(function.name).second) error(diagnostics, "duplicate machine function @" + function.name);
        if (function.blocks.empty()) { error(diagnostics, "machine function @" + function.name + " has no blocks"); continue; }
        std::unordered_map<std::string, const Block*> blocks;
        std::vector<bool> defined(function.register_count, false);
        if (function.register_widths.size() != function.register_count) error(diagnostics, "machine register-width table mismatch in @" + function.name);
        if (function.register_classes.size() != function.register_count) error(diagnostics, "machine register-class table mismatch in @" + function.name);
        if (function.argument_widths.size() != function.argument_count || function.argument_classes.size() != function.argument_count) error(diagnostics, "machine argument metadata mismatch in @" + function.name);
        const auto verify_argument_groups = [&](const std::vector<std::uint8_t>& sizes,
                                                const std::vector<std::uint8_t>& alignments,
                                                std::size_t slot_count, std::size_t first_slot,
                                                const std::string& owner) {
            if (sizes.empty() && alignments.empty()) return;
            if (sizes.size() != slot_count || alignments.size() != slot_count) {
                error(diagnostics, "machine argument-group metadata mismatch in " + owner);
                return;
            }
            std::size_t cursor = first_slot;
            while (cursor < slot_count) {
                const auto group = static_cast<std::size_t>(sizes[cursor]);
                const auto alignment = alignments[cursor];
                const bool valid_alignment = alignment == 1U || alignment == 2U || alignment == 4U ||
                    alignment == 8U || alignment == 16U;
                if (group == 0U || cursor + group > slot_count || !valid_alignment) {
                    error(diagnostics, "invalid machine argument group in " + owner +
                        " at slot " + std::to_string(cursor) + " (group=" + std::to_string(group) +
                        ", alignment=" + std::to_string(alignments[cursor]) +
                        ", slots=" + std::to_string(slot_count) + ")");
                    return;
                }
                for (std::size_t piece = 1U; piece < group; ++piece) {
                    if (sizes[cursor + piece] != 0U || alignments[cursor + piece] != 0U) {
                        error(diagnostics, "invalid machine argument-group continuation in " + owner);
                        return;
                    }
                }
                cursor += group;
            }
        };
        verify_argument_groups(function.argument_group_sizes, function.argument_group_alignments,
                               function.argument_count, 0U, "@" + function.name);
        for (const auto width : function.argument_widths) {
            if (width != 1U && width != 2U && width != 4U && width != 8U && width != 16U) {
                error(diagnostics, "invalid machine argument width in @" + function.name);
                break;
            }
        }
        for (const auto& block : function.blocks) {
            if (!blocks.emplace(block.name, &block).second) error(diagnostics, "duplicate machine block " + block.name + " in @" + function.name);
            for (auto reg : block.parameters) {
                if (reg >= function.register_count) error(diagnostics, "block parameter out of range in @" + function.name);
                else if (defined[reg]) error(diagnostics, "virtual register v" + std::to_string(reg) + " defined more than once in @" + function.name);
                else defined[reg] = true;
            }
        }
        for (const auto& block : function.blocks) {
            if (block.instructions.empty()) { error(diagnostics, "machine block " + block.name + " is empty"); continue; }
            for (std::size_t i = 0; i < block.instructions.size(); ++i) {
                const auto& ins = block.instructions[i];
                const auto immediate_store_inputs = ins.opcode == Opcode::store_ptr_i8 || ins.opcode == Opcode::store_ptr_i16 ||
                    ins.opcode == Opcode::store_ptr_i32 || ins.opcode == Opcode::store_ptr_i64 ? 1U : 0U;
                const auto expected = (ins.symbol == "$imm" || ins.symbol == "$memstack") ? 1U :
                    (ins.symbol == "$fcmp" ? 2U :
                     (ins.symbol == "$cmpimm" ? 1U :
                     (ins.symbol == "$testimm" ? ((ins.opcode == Opcode::select_i32 || ins.opcode == Opcode::select_i64) ? 3U : 1U) :
                     (ins.symbol == "$flags" ? 1U :
                     (ins.symbol == "$retimm" || ins.symbol == "$retloadstack" ? 0U :
                      (ins.symbol == "$storeimm" ? immediate_store_inputs :
                       (ins.opcode == Opcode::branch_i1 && ins.immediate != 0 ? 2U : expected_inputs(ins.opcode))))))));
                if (ins.symbol == "$imm" && !supports_integer_immediate(ins.opcode))
                    error(diagnostics, "immediate operand on unsupported opcode in @" + function.name);
                if (ins.symbol == "$memstack" && !supports_integer_immediate(ins.opcode))
                    error(diagnostics, "memory operand on unsupported opcode in @" + function.name);
                if (ins.symbol == "$cmpimm" && ins.opcode != Opcode::branch_i1 && !is_integer_comparison(ins.opcode))
                    error(diagnostics, "immediate comparison on unsupported opcode in @" + function.name);
                if (ins.symbol == "$testimm" && ins.opcode != Opcode::branch_i1 && ins.opcode != Opcode::select_i32 && ins.opcode != Opcode::select_i64)
                    error(diagnostics, "test-immediate branch form on unsupported opcode in @" + function.name);
                if (ins.symbol == "$flags" && ins.opcode != Opcode::branch_i1)
                    error(diagnostics, "arithmetic-flags branch form on unsupported opcode in @" + function.name);
                if (ins.symbol == "$fcmp" && ins.opcode != Opcode::branch_i1)
                    error(diagnostics, "floating comparison branch form on unsupported opcode in @" + function.name);
                if ((ins.symbol == "$retimm" || ins.symbol == "$retloadstack") && ins.opcode != Opcode::return_i32 && ins.opcode != Opcode::return_i64)
                    error(diagnostics, "direct return form on unsupported opcode in @" + function.name);
                if (ins.symbol == "$retloadstack" && ins.argument_index != 1U && ins.argument_index != 2U &&
                    ins.argument_index != 4U && ins.argument_index != 8U)
                    error(diagnostics, "invalid folded return load width in @" + function.name);
                if (ins.symbol == "$storeimm" && ins.opcode != Opcode::store_stack_i8 && ins.opcode != Opcode::store_stack_i16 &&
                    ins.opcode != Opcode::store_stack_i32 && ins.opcode != Opcode::store_stack_i64 &&
                    ins.opcode != Opcode::store_ptr_i8 && ins.opcode != Opcode::store_ptr_i16 &&
                    ins.opcode != Opcode::store_ptr_i32 && ins.opcode != Opcode::store_ptr_i64)
                    error(diagnostics, "immediate store on unsupported opcode in @" + function.name);
                if (expected != static_cast<std::size_t>(-1) && ins.inputs.size() != expected) error(diagnostics, "wrong operand count for " + std::string(opcode_name(ins.opcode)) + " in @" + function.name);
                if (is_packed_binary(ins.opcode)) verify_packed_binary(ins, function.name, diagnostics);
                if (ins.opcode == Opcode::reduce_add_i32_contiguous || ins.opcode == Opcode::reduce_add_i64_contiguous) {
                    const auto minimum = ins.opcode == Opcode::reduce_add_i64_contiguous ? 2 : 4;
                    const auto maximum = ins.opcode == Opcode::reduce_add_i64_contiguous ? 16 : 32;
                    if (ins.immediate < minimum || ins.immediate > maximum ||
                        (ins.immediate & (ins.immediate - 1)) != 0)
                        error(diagnostics, "invalid packed reduction lane count in @" + function.name);
                }
                for (auto reg : ins.inputs) if (reg >= function.register_count) error(diagnostics, "input virtual register out of range in @" + function.name);
                if ((ins.opcode == Opcode::call_i32 || ins.opcode == Opcode::call_i64 || ins.opcode == Opcode::call_f32 || ins.opcode == Opcode::call_f64 || ins.opcode == Opcode::call_void || ins.opcode == Opcode::call_aggregate || ins.opcode == Opcode::load_function_address || (ins.opcode == Opcode::load_global_address || ins.opcode == Opcode::load_tls_address)) && ins.symbol.empty()) error(diagnostics, "call has empty target in @" + function.name);
                if ((ins.opcode == Opcode::call_indirect_i32 || ins.opcode == Opcode::call_indirect_i64 || ins.opcode == Opcode::call_indirect_f32 || ins.opcode == Opcode::call_indirect_f64 || ins.opcode == Opcode::call_indirect_void) && ins.inputs.empty()) error(diagnostics, "indirect call has no target in @" + function.name);
                if (ins.opcode == Opcode::call_i32 || ins.opcode == Opcode::call_i64 ||
                    ins.opcode == Opcode::call_f32 || ins.opcode == Opcode::call_f64 ||
                    ins.opcode == Opcode::call_void || ins.opcode == Opcode::call_aggregate ||
                    ins.opcode == Opcode::call_indirect_i32 || ins.opcode == Opcode::call_indirect_i64 ||
                    ins.opcode == Opcode::call_indirect_f32 || ins.opcode == Opcode::call_indirect_f64 ||
                    ins.opcode == Opcode::call_indirect_void) {
                    std::size_t first_argument = (ins.opcode == Opcode::call_indirect_i32 ||
                        ins.opcode == Opcode::call_indirect_i64 || ins.opcode == Opcode::call_indirect_f32 ||
                        ins.opcode == Opcode::call_indirect_f64 || ins.opcode == Opcode::call_indirect_void) ? 1U : 0U;
                    if (ins.indirect_result || ins.opcode == Opcode::call_aggregate) ++first_argument;
                    verify_argument_groups(ins.argument_group_sizes, ins.argument_group_alignments,
                                           ins.inputs.size(), std::min(first_argument, ins.inputs.size()),
                                           "call in @" + function.name);
                    const auto ordinary_argument_count = ins.inputs.size() - std::min(first_argument, ins.inputs.size());
                    if (!ins.argument_widths.empty()) {
                        if (ins.argument_widths.size() != ins.inputs.size()) {
                            error(diagnostics, "machine call argument-width metadata mismatch in @" + function.name);
                        } else {
                            for (std::size_t slot = first_argument; slot < ins.argument_widths.size(); ++slot) {
                                const auto width = ins.argument_widths[slot];
                                if (width != 1U && width != 2U && width != 4U && width != 8U && width != 16U) {
                                    error(diagnostics, "invalid machine call argument width in @" + function.name);
                                    break;
                                }
                            }
                        }
                    }
                    if (!ins.variadic_call && ins.variadic_named_input_count != 0U)
                        error(diagnostics, "non-variadic machine call has variadic boundary metadata in @" + function.name);
                    if (ins.variadic_call && ins.variadic_named_input_count > ordinary_argument_count)
                        error(diagnostics, "variadic machine call named-argument boundary is out of range in @" + function.name);
                    if (ins.variadic_call && !ins.argument_group_sizes.empty() &&
                        ins.variadic_named_input_count < ordinary_argument_count) {
                        const auto boundary = first_argument + ins.variadic_named_input_count;
                        if (boundary < ins.argument_group_sizes.size() && ins.argument_group_sizes[boundary] == 0U)
                            error(diagnostics, "variadic machine call boundary splits an argument group in @" + function.name);
                    }
                }
                const auto stack_access_size = [&]() -> std::int64_t {
                    switch (ins.opcode) {
                    case Opcode::load_stack_i8: case Opcode::store_stack_i8: return 1;
                    case Opcode::load_stack_i16: case Opcode::store_stack_i16: return 2;
                    case Opcode::load_stack_i32: case Opcode::store_stack_i32: return 4;
                    case Opcode::load_stack_i64: case Opcode::store_stack_i64: case Opcode::load_stack_f64: case Opcode::store_stack_f64: return 8;
                    case Opcode::load_stack_f32: case Opcode::store_stack_f32: return 4;
                    case Opcode::load_stack_v128: case Opcode::store_stack_v128: return 16;
                    case Opcode::load_stack_v256: case Opcode::store_stack_v256: return 32;
                    case Opcode::load_stack_v512: case Opcode::store_stack_v512: return 64;
                    default: return 0;
                    }
                }();
                if (stack_access_size != 0 &&
                    (ins.immediate > -stack_access_size || -ins.immediate > static_cast<std::int64_t>(function.local_stack_size)))
                    error(diagnostics, "stack memory offset out of range in @" + function.name);
                if (ins.opcode == Opcode::load_stack_address &&
                    (ins.immediate >= 0 || -ins.immediate > static_cast<std::int64_t>(function.local_stack_size)))
                    error(diagnostics, "stack address offset out of range in @" + function.name);
                if (ins.opcode == Opcode::store_stack_v128 || ins.opcode == Opcode::store_stack_v256 ||
                    ins.opcode == Opcode::store_stack_v512) {
                    for (const auto reg : ins.inputs)
                        if (reg < function.register_classes.size() && function.register_classes[reg] != RegisterClass::vector)
                            error(diagnostics, "packed stack store has non-vector input register in @" + function.name);
                }
                if (has_result(ins.opcode)) {
                    if (ins.result >= function.register_count) error(diagnostics, "result virtual register out of range in @" + function.name);
                    else {
                        if (ins.result < function.register_classes.size()) {
                            const auto result_class = function.register_classes[ins.result];
                            if (is_float_arithmetic(ins.opcode) && result_class != RegisterClass::floating)
                                error(diagnostics, "floating opcode has non-floating result register in @" + function.name);
                            if (is_float_comparison(ins.opcode) && result_class != RegisterClass::integer)
                                error(diagnostics, "floating comparison has non-integer result register in @" + function.name);
                            if ((ins.opcode == Opcode::load_stack_v128 || ins.opcode == Opcode::load_stack_v256 ||
                                 ins.opcode == Opcode::load_stack_v512) && result_class != RegisterClass::vector)
                                error(diagnostics, "packed stack load has non-vector result register in @" + function.name);
                        }
                        if (defined[ins.result]) error(diagnostics, "virtual register v" + std::to_string(ins.result) + " defined more than once in @" + function.name);
                        else defined[ins.result] = true;
                    }
                }
                if (requires_float_inputs(ins.opcode)) {
                    for (const auto reg : ins.inputs) {
                        if (reg < function.register_classes.size() && function.register_classes[reg] != RegisterClass::floating)
                            error(diagnostics, "floating opcode has non-floating input register in @" + function.name);
                    }
                }
                if (is_terminator(ins.opcode) && i + 1 != block.instructions.size()) error(diagnostics, "machine terminator is not last in block " + block.name);
                const std::size_t expected_successors = ins.opcode == Opcode::jump ? 1U : ins.opcode == Opcode::branch_i1 ? 2U : 0U;
                if (ins.successors.size() != expected_successors) error(diagnostics, "wrong successor count for " + std::string(opcode_name(ins.opcode)) + " in @" + function.name);
            }
            if (!is_terminator(block.instructions.back().opcode)) error(diagnostics, "machine block " + block.name + " has no terminator");
        }
        for (const auto& block : function.blocks) for (const auto& ins : block.instructions) for (const auto& successor : ins.successors) {
            const auto target = blocks.find(successor.block);
            if (target == blocks.end()) { error(diagnostics, "unknown machine successor " + successor.block + " in @" + function.name); continue; }
            if (successor.arguments.size() != target->second->parameters.size()) error(diagnostics, "machine edge argument mismatch for " + successor.block + " in @" + function.name);
            for (auto reg : successor.arguments) if (reg >= function.register_count) error(diagnostics, "edge virtual register out of range in @" + function.name);
        }
        for (std::uint32_t reg = 0; reg < function.register_count; ++reg) if (!defined[reg]) error(diagnostics, "virtual register v" + std::to_string(reg) + " has no definition in @" + function.name);
    }
    return diagnostics;
}
}
