// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace forge::machine {

using VirtualRegister = std::uint32_t;

// `vector` values live in the same XMM/YMM/ZMM physical file as `floating`
// ones, so the two classes are allocated from a single pool. Giving vectors
// their own pool over the same registers would let the allocator hand xmm2 to a
// float and a packed value at the same time.
enum class RegisterClass : std::uint8_t { integer, floating, vector };

[[nodiscard]] constexpr bool uses_vector_register_file(RegisterClass value) noexcept {
    return value == RegisterClass::floating || value == RegisterClass::vector;
}

enum class Opcode : std::uint8_t {
    load_argument,
    load_argument_i64,
    load_argument_f32,
    load_argument_f64,
    load_immediate,
    load_immediate_i64,
    load_immediate_f32,
    load_immediate_f64,
    load_function_address,
    load_global_address,
    load_tls_address,
    load_stack_address,
    ptr_offset,
    copy,
    copy_f32,
    copy_f64,
    add_f32,
    add_f64,
    sub_f32,
    sub_f64,
    mul_f32,
    mul_f64,
    div_f32,
    div_f64,
    neg_f32,
    neg_f64,
    cmp_eq_f32,
    cmp_ne_f32,
    cmp_lt_f32,
    cmp_le_f32,
    cmp_gt_f32,
    cmp_ge_f32,
    cmp_eq_f64,
    cmp_ne_f64,
    cmp_lt_f64,
    cmp_le_f64,
    cmp_gt_f64,
    cmp_ge_f64,
    add_i32,
    add_i64,
    sub_i32,
    sub_i64,
    mul_i32,
    mul_i64,
    div_s_i32,
    div_s_i64,
    div_u_i32,
    div_u_i64,
    rem_s_i32,
    rem_s_i64,
    rem_u_i32,
    rem_u_i64,
    and_i32,
    and_i64,
    or_i32,
    or_i64,
    xor_i32,
    xor_i64,
    // SLP-formed packed forms. These are memory-to-memory: the inputs are base
    // pointers and `immediate` carries the lane count, so a single instruction
    // stands for a whole unrolled run of scalar lanes. `argument_index` holds
    // the scalar Opcode each lane performs and `vector_bits` the width the cost
    // model selected.
    reduce_add_i32_contiguous,
    reduce_add_i64_contiguous,
    add_i64_contiguous_inplace,
    binary_i32_contiguous_inplace,
    binary_i64_contiguous_inplace,
    binary_i32_contiguous_map,
    binary_i64_contiguous_map,
    binary_i32_contiguous_map2,
    binary_i64_contiguous_map2,
    binary_i32_contiguous_map3,
    binary_i64_contiguous_map3,
    binary_i32_contiguous_chain,
    binary_i64_contiguous_chain,
    binary_i32_contiguous_dag,
    binary_i64_contiguous_dag,
    binary_i32_contiguous_dag_reuse,
    binary_i64_contiguous_dag_reuse,
    select_i32,
    select_i64,
    shl_i32,
    shl_i64,
    shr_s_i32,
    shr_s_i64,
    shr_u_i32,
    shr_u_i64,
    neg_i32,
    neg_i64,
    not_i32,
    not_i64,
    zero_extend,
    sign_extend,
    truncate,
    int_to_float_signed,
    int_to_float_unsigned,
    float_to_int_signed,
    float_to_int_unsigned,
    float_extend,
    float_truncate,
    cmp_eq_i32,
    cmp_ne_i32,
    cmp_lt_i32,
    cmp_le_i32,
    cmp_gt_i32,
    cmp_ge_i32,
    cmp_ult_i32,
    cmp_ule_i32,
    cmp_ugt_i32,
    cmp_uge_i32,
    cmp_eq_i64,
    cmp_ne_i64,
    cmp_lt_i64,
    cmp_le_i64,
    cmp_gt_i64,
    cmp_ge_i64,
    cmp_ult_i64,
    cmp_ule_i64,
    cmp_ugt_i64,
    cmp_uge_i64,
    load_stack_i8,
    load_stack_i16,
    load_stack_i32,
    load_stack_i64,
    load_stack_f32,
    load_stack_f64,
    store_stack_i8,
    store_stack_i16,
    store_stack_i32,
    store_stack_i64,
    store_stack_f32,
    store_stack_f64,
    // Packed spill and reload. These use unaligned moves so a spill slot only
    // has to be non-overlapping, not aligned to its width.
    load_stack_v128,
    load_stack_v256,
    load_stack_v512,
    store_stack_v128,
    store_stack_v256,
    store_stack_v512,
    load_ptr_i8,
    load_ptr_i16,
    load_ptr_i32,
    store_ptr_i8,
    store_ptr_i16,
    store_ptr_i32,
    load_ptr_i64,
    load_ptr_f32,
    load_ptr_f64,
    store_ptr_i64,
    store_ptr_f32,
    store_ptr_f64,
    call_i32,
    call_i64,
    call_f32,
    call_f64,
    call_void,
    call_aggregate,
    call_indirect_i32,
    call_indirect_i64,
    call_indirect_f32,
    call_indirect_f64,
    call_indirect_void,
    jump,
    branch_i1,
    return_i32,
    return_i64,
    return_f32,
    return_f64,
    return_void,
    return_aggregate,
};

struct Successor {
    std::string block;
    std::vector<VirtualRegister> arguments;
};

struct Instruction {
    Opcode opcode{};
    VirtualRegister result{};
    std::vector<VirtualRegister> inputs;
    std::int64_t immediate{};
    std::uint32_t argument_index{};
    std::string symbol;
    std::vector<Successor> successors;
    // Selected packed width for target-aware vector pseudos, in bits. Zero
    // means the instruction is scalar, so every existing opcode keeps its
    // current meaning without being rewritten.
    std::uint16_t vector_bits{};
    // Active lane count for masked AVX-512 emission. Zero means unmasked, so a
    // fully populated vector needs no opmask register.
    std::uint8_t vector_mask_lanes{};
    // AAPCS64 returns large aggregates through x8 instead of consuming x0.
    // This bit marks a call whose first machine input is that hidden result
    // pointer, allowing target encoders to keep the shared call IR intact.
    bool indirect_result{};
    // Native variadic calls need the source-level boundary between named and
    // anonymous arguments after aggregate values have been flattened into
    // machine inputs. Generic AAPCS64 still allocates anonymous arguments
    // through x0-x7/v0-v7, while Darwin arm64 places the anonymous tail on the
    // stack. `variadic_named_input_count` counts only ordinary call-argument
    // inputs (it excludes an indirect target and hidden aggregate-result
    // destination prefixes).
    bool variadic_call{};
    std::uint32_t variadic_named_input_count{};
    // AAPCS64 needs source-level argument boundaries after aggregate values have
    // been flattened into machine pieces. A nonzero entry marks the first input
    // of one argument and gives its piece count; continuation entries are zero.
    // Alignments are normalized stack alignments (8 or 16 bytes).
    std::vector<std::uint8_t> argument_group_sizes;
    std::vector<std::uint8_t> argument_group_alignments;
    // ABI storage width for each call input. Prefix inputs such as an indirect
    // target or hidden result destination use zero. Keeping source widths here
    // lets Darwin arm64 pack fixed stack arguments at their natural byte size
    // while generic AAPCS64 can still round stack slots according to the PCS.
    std::vector<std::uint8_t> argument_widths;

    Instruction() = default;
    Instruction(Opcode opcode_value, VirtualRegister result_value,
                std::vector<VirtualRegister> input_values = {}, std::int64_t immediate_value = 0,
                std::uint32_t argument_index_value = 0, std::string symbol_value = {},
                std::vector<Successor> successor_values = {}, std::uint16_t vector_bits_value = 0,
                std::uint8_t vector_mask_lanes_value = 0, bool indirect_result_value = false,
                bool variadic_call_value = false, std::uint32_t variadic_named_input_count_value = 0)
        : opcode(opcode_value), result(result_value), inputs(std::move(input_values)),
          immediate(immediate_value), argument_index(argument_index_value), symbol(std::move(symbol_value)),
          successors(std::move(successor_values)), vector_bits(vector_bits_value),
          vector_mask_lanes(vector_mask_lanes_value), indirect_result(indirect_result_value),
          variadic_call(variadic_call_value), variadic_named_input_count(variadic_named_input_count_value) {}
};

struct Block {
    std::string name;
    std::vector<VirtualRegister> parameters;
    std::vector<Instruction> instructions;
};

struct Function {
    std::string name;
    // Per-function ISA requirement propagated from FIR. Target encoders consume
    // this instead of silently discarding frontend target_feature metadata.
    std::string target_feature;
    std::uint32_t argument_count{};
    // True when machine argument slot zero is a hidden aggregate-result
    // pointer. On AAPCS64 this slot arrives in x8 rather than x0.
    bool indirect_result_parameter{};
    std::vector<std::uint8_t> argument_widths;
    std::vector<RegisterClass> argument_classes;
    // Same convention as Instruction::argument_group_sizes, indexed by machine
    // argument slot. This prevents AAPCS64 composites/HFAs from being partially
    // allocated when only part of a register bank remains.
    std::vector<std::uint8_t> argument_group_sizes;
    std::vector<std::uint8_t> argument_group_alignments;
    VirtualRegister register_count{};
    std::vector<std::uint8_t> register_widths;
    std::vector<RegisterClass> register_classes;
    std::uint32_t local_stack_size{};
    std::uint32_t machine_instructions_before_optimization{};
    std::uint32_t machine_copies_propagated{};
    std::uint32_t machine_zero_offsets_eliminated{};
    std::uint32_t machine_redundant_casts_eliminated{};
    std::uint32_t machine_address_modes_folded{};
    std::uint32_t machine_compare_branches_fused{};
    std::uint32_t machine_compare_branch_bytes_avoided{};
    std::uint32_t machine_floating_compare_branches_fused{};
    std::uint32_t machine_floating_compare_branch_bytes_avoided{};
    std::uint32_t machine_jump_threads{};
    std::uint32_t machine_empty_blocks_removed{};
    std::uint32_t machine_unreachable_blocks_removed{};
    std::uint32_t machine_blocks_reordered{};
    std::uint32_t machine_immediate_forms_selected{};
    std::uint32_t machine_constant_definitions_eliminated{};
    std::uint32_t machine_immediate_comparisons_selected{};
    std::uint32_t machine_direct_constant_returns{};
    std::uint32_t machine_zeroing_idioms_selected{};
    std::uint32_t machine_constant_stores_selected{};
    std::uint32_t machine_extension_chains_eliminated{};
    std::uint32_t machine_load_returns_folded{};
    std::uint32_t machine_load_arithmetic_folded{};
    std::uint32_t machine_dead_instructions_eliminated{};
    std::uint32_t machine_dead_comparisons_eliminated{};
    std::uint32_t machine_cross_block_copies_propagated{};
    std::uint32_t machine_liveness_iterations{};
    std::uint32_t machine_cross_block_live_values{};
    std::vector<Block> blocks;
};

struct Global {
    std::string name;
    std::uint32_t size{};
    std::uint32_t alignment{1};
    bool is_constant{};
    bool is_external{};
    bool is_thread_local{};
    bool is_internal{};
    std::vector<std::uint8_t> initializer;
};

struct Module {
    std::string name;
    std::vector<Global> globals;
    std::vector<Function> functions;
};

[[nodiscard]] const char* opcode_name(Opcode opcode) noexcept;
[[nodiscard]] std::string print_module(const Module& module);

} // namespace forge::machine
