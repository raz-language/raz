// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/machine/module.hpp"

#include <sstream>

namespace forge::machine {

const char* opcode_name(Opcode opcode) noexcept {
    switch (opcode) {
    case Opcode::load_argument: return "load_argument";
    case Opcode::load_argument_i64: return "load_argument_i64";
    case Opcode::load_argument_f32: return "load_argument_f32";
    case Opcode::load_argument_f64: return "load_argument_f64";
    case Opcode::load_immediate: return "load_immediate";
    case Opcode::load_immediate_i64: return "load_immediate_i64";
    case Opcode::load_immediate_f32: return "load_immediate_f32";
    case Opcode::load_immediate_f64: return "load_immediate_f64";
    case Opcode::load_function_address: return "load_function_address";
    case Opcode::load_global_address: return "load_global_address";
    case Opcode::load_tls_address: return "load_tls_address";
    case Opcode::load_stack_address: return "load_stack_address";
    case Opcode::ptr_offset: return "ptr_offset";
    case Opcode::copy: return "copy";
    case Opcode::copy_f32: return "copy_f32";
    case Opcode::copy_f64: return "copy_f64";
    case Opcode::add_f32: return "add_f32";
    case Opcode::add_f64: return "add_f64";
    case Opcode::sub_f32: return "sub_f32";
    case Opcode::sub_f64: return "sub_f64";
    case Opcode::mul_f32: return "mul_f32";
    case Opcode::mul_f64: return "mul_f64";
    case Opcode::div_f32: return "div_f32";
    case Opcode::div_f64: return "div_f64";
    case Opcode::neg_f32: return "neg_f32";
    case Opcode::neg_f64: return "neg_f64";
    case Opcode::cmp_eq_f32: return "cmp_eq_f32";
    case Opcode::cmp_ne_f32: return "cmp_ne_f32";
    case Opcode::cmp_lt_f32: return "cmp_lt_f32";
    case Opcode::cmp_le_f32: return "cmp_le_f32";
    case Opcode::cmp_gt_f32: return "cmp_gt_f32";
    case Opcode::cmp_ge_f32: return "cmp_ge_f32";
    case Opcode::cmp_eq_f64: return "cmp_eq_f64";
    case Opcode::cmp_ne_f64: return "cmp_ne_f64";
    case Opcode::cmp_lt_f64: return "cmp_lt_f64";
    case Opcode::cmp_le_f64: return "cmp_le_f64";
    case Opcode::cmp_gt_f64: return "cmp_gt_f64";
    case Opcode::cmp_ge_f64: return "cmp_ge_f64";
    case Opcode::add_i32: return "add_i32";
    case Opcode::add_i64: return "add_i64";
    case Opcode::sub_i32: return "sub_i32";
    case Opcode::sub_i64: return "sub_i64";
    case Opcode::mul_i32: return "mul_i32";
    case Opcode::mul_i64: return "mul_i64";
    case Opcode::div_s_i32: return "div_s_i32";
    case Opcode::div_s_i64: return "div_s_i64";
    case Opcode::div_u_i32: return "div_u_i32";
    case Opcode::div_u_i64: return "div_u_i64";
    case Opcode::rem_s_i32: return "rem_s_i32";
    case Opcode::rem_s_i64: return "rem_s_i64";
    case Opcode::rem_u_i32: return "rem_u_i32";
    case Opcode::rem_u_i64: return "rem_u_i64";
    case Opcode::and_i32: return "and_i32";
    case Opcode::and_i64: return "and_i64";
    case Opcode::or_i32: return "or_i32";
    case Opcode::or_i64: return "or_i64";
    case Opcode::xor_i32: return "xor_i32";
    case Opcode::xor_i64: return "xor_i64";
    case Opcode::reduce_add_i32_contiguous: return "reduce_add_i32_contiguous";
    case Opcode::reduce_add_i64_contiguous: return "reduce_add_i64_contiguous";
    case Opcode::add_i64_contiguous_inplace: return "add_i64_contiguous_inplace";
    case Opcode::binary_i32_contiguous_inplace: return "binary_i32_contiguous_inplace";
    case Opcode::binary_i64_contiguous_inplace: return "binary_i64_contiguous_inplace";
    case Opcode::binary_i32_contiguous_map: return "binary_i32_contiguous_map";
    case Opcode::binary_i64_contiguous_map: return "binary_i64_contiguous_map";
    case Opcode::binary_i32_contiguous_map2: return "binary_i32_contiguous_map2";
    case Opcode::binary_i64_contiguous_map2: return "binary_i64_contiguous_map2";
    case Opcode::binary_i32_contiguous_map3: return "binary_i32_contiguous_map3";
    case Opcode::binary_i64_contiguous_map3: return "binary_i64_contiguous_map3";
    case Opcode::binary_i32_contiguous_chain: return "binary_i32_contiguous_chain";
    case Opcode::binary_i64_contiguous_chain: return "binary_i64_contiguous_chain";
    case Opcode::binary_i32_contiguous_dag: return "binary_i32_contiguous_dag";
    case Opcode::binary_i64_contiguous_dag: return "binary_i64_contiguous_dag";
    case Opcode::binary_i32_contiguous_dag_reuse: return "binary_i32_contiguous_dag_reuse";
    case Opcode::binary_i64_contiguous_dag_reuse: return "binary_i64_contiguous_dag_reuse";
    case Opcode::select_i32: return "select_i32";
    case Opcode::select_i64: return "select_i64";
    case Opcode::shl_i32: return "shl_i32";
    case Opcode::shl_i64: return "shl_i64";
    case Opcode::shr_s_i32: return "shr_s_i32";
    case Opcode::shr_s_i64: return "shr_s_i64";
    case Opcode::shr_u_i32: return "shr_u_i32";
    case Opcode::shr_u_i64: return "shr_u_i64";
    case Opcode::neg_i32: return "neg_i32";
    case Opcode::neg_i64: return "neg_i64";
    case Opcode::not_i32: return "not_i32";
    case Opcode::not_i64: return "not_i64";
    case Opcode::zero_extend: return "zero_extend";
    case Opcode::sign_extend: return "sign_extend";
    case Opcode::truncate: return "truncate";
    case Opcode::int_to_float_signed: return "int_to_float_signed";
    case Opcode::int_to_float_unsigned: return "int_to_float_unsigned";
    case Opcode::float_to_int_signed: return "float_to_int_signed";
    case Opcode::float_to_int_unsigned: return "float_to_int_unsigned";
    case Opcode::float_extend: return "float_extend";
    case Opcode::float_truncate: return "float_truncate";
    case Opcode::cmp_eq_i32: return "cmp_eq_i32";
    case Opcode::cmp_ne_i32: return "cmp_ne_i32";
    case Opcode::cmp_lt_i32: return "cmp_lt_i32";
    case Opcode::cmp_le_i32: return "cmp_le_i32";
    case Opcode::cmp_gt_i32: return "cmp_gt_i32";
    case Opcode::cmp_ge_i32: return "cmp_ge_i32";
    case Opcode::cmp_ult_i32: return "cmp_ult_i32";
    case Opcode::cmp_ule_i32: return "cmp_ule_i32";
    case Opcode::cmp_ugt_i32: return "cmp_ugt_i32";
    case Opcode::cmp_uge_i32: return "cmp_uge_i32";
    case Opcode::cmp_eq_i64: return "cmp_eq_i64";
    case Opcode::cmp_ne_i64: return "cmp_ne_i64";
    case Opcode::cmp_lt_i64: return "cmp_lt_i64";
    case Opcode::cmp_le_i64: return "cmp_le_i64";
    case Opcode::cmp_gt_i64: return "cmp_gt_i64";
    case Opcode::cmp_ge_i64: return "cmp_ge_i64";
    case Opcode::cmp_ult_i64: return "cmp_ult_i64";
    case Opcode::cmp_ule_i64: return "cmp_ule_i64";
    case Opcode::cmp_ugt_i64: return "cmp_ugt_i64";
    case Opcode::cmp_uge_i64: return "cmp_uge_i64";
    case Opcode::load_stack_i8: return "load_stack_i8";
    case Opcode::load_stack_i16: return "load_stack_i16";
    case Opcode::load_stack_i32: return "load_stack_i32";
    case Opcode::load_stack_i64: return "load_stack_i64";
    case Opcode::load_stack_f32: return "load_stack_f32";
    case Opcode::load_stack_f64: return "load_stack_f64";
    case Opcode::store_stack_i8: return "store_stack_i8";
    case Opcode::store_stack_i16: return "store_stack_i16";
    case Opcode::store_stack_i32: return "store_stack_i32";
    case Opcode::store_stack_i64: return "store_stack_i64";
    case Opcode::store_stack_f32: return "store_stack_f32";
    case Opcode::store_stack_f64: return "store_stack_f64";
    case Opcode::load_stack_v128: return "load_stack_v128";
    case Opcode::load_stack_v256: return "load_stack_v256";
    case Opcode::load_stack_v512: return "load_stack_v512";
    case Opcode::store_stack_v128: return "store_stack_v128";
    case Opcode::store_stack_v256: return "store_stack_v256";
    case Opcode::store_stack_v512: return "store_stack_v512";
    case Opcode::load_ptr_i8: return "load_ptr_i8";
    case Opcode::load_ptr_i16: return "load_ptr_i16";
    case Opcode::load_ptr_i32: return "load_ptr_i32";
    case Opcode::store_ptr_i8: return "store_ptr_i8";
    case Opcode::store_ptr_i16: return "store_ptr_i16";
    case Opcode::store_ptr_i32: return "store_ptr_i32";
    case Opcode::load_ptr_i64: return "load_ptr_i64";
    case Opcode::load_ptr_f32: return "load_ptr_f32";
    case Opcode::load_ptr_f64: return "load_ptr_f64";
    case Opcode::store_ptr_i64: return "store_ptr_i64";
    case Opcode::store_ptr_f32: return "store_ptr_f32";
    case Opcode::store_ptr_f64: return "store_ptr_f64";
    case Opcode::call_i32: return "call_i32";
    case Opcode::call_i64: return "call_i64";
    case Opcode::call_f32: return "call_f32";
    case Opcode::call_f64: return "call_f64";
    case Opcode::call_void: return "call_void";
    case Opcode::call_aggregate: return "call_aggregate";
    case Opcode::call_indirect_i32: return "call_indirect_i32";
    case Opcode::call_indirect_i64: return "call_indirect_i64";
    case Opcode::call_indirect_f32: return "call_indirect_f32";
    case Opcode::call_indirect_f64: return "call_indirect_f64";
    case Opcode::call_indirect_void: return "call_indirect_void";
    case Opcode::jump: return "jump";
    case Opcode::branch_i1: return "branch_i1";
    case Opcode::return_i32: return "return_i32";
    case Opcode::return_i64: return "return_i64";
    case Opcode::return_f32: return "return_f32";
    case Opcode::return_f64: return "return_f64";
    case Opcode::return_void: return "return_void";
    case Opcode::return_aggregate: return "return_aggregate";
    }
    return "invalid";
}

namespace {
// Packed DAG and chain forms store their opcode program in `symbol` as
// little-endian 16-bit tokens, so it survives alongside the operand list
// without widening Instruction.
std::uint32_t packed_token(const std::string& program, std::size_t offset) {
    return static_cast<std::uint32_t>(static_cast<unsigned char>(program[offset])) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(program[offset + 1U])) << 8U);
}

void print_successor(std::ostringstream& out, const Successor& successor) {
    out << successor.block;
    if (!successor.arguments.empty()) {
        out << '(';
        for (std::size_t index = 0; index < successor.arguments.size(); ++index) {
            if (index != 0) out << ", ";
            out << 'v' << successor.arguments[index];
        }
        out << ')';
    }
}
}

std::string print_module(const Module& module) {
    std::ostringstream out;
    out << "machine.module @" << module.name << " {\n";
    for (const auto& global : module.globals) {
        out << "  machine." << (global.is_external ? "external." : "")
            << (global.is_constant ? "constant" : "global") << " @" << global.name
            << " size " << global.size;
        if (!global.is_external) {
            out << " init [";
            for (std::size_t index = 0; index < global.initializer.size(); ++index) {
                if (index) out << ',';
                out << static_cast<unsigned>(global.initializer[index]);
            }
            out << ']';
        }
        out << "\n";
    }

    if (!module.globals.empty() && !module.functions.empty()) out << "\n";
    for (const auto& function : module.functions) {
        out << "  machine.func @" << function.name << " args " << function.argument_count
            << " vregs " << function.register_count << " stack " << function.local_stack_size << " {\n";
        for (const auto& block : function.blocks) {
            out << "  " << block.name;
            if (!block.parameters.empty()) {
                out << '(';
                for (std::size_t index = 0; index < block.parameters.size(); ++index) {
                    if (index != 0) out << ", ";
                    out << 'v' << block.parameters[index];
                }
                out << ')';
            }
            out << ":\n";
            for (const auto& instruction : block.instructions) {
                out << "    ";
                const bool has_result = instruction.opcode != Opcode::return_i32 &&
                                        instruction.opcode != Opcode::return_i64 &&
                                        instruction.opcode != Opcode::return_f32 &&
                                        instruction.opcode != Opcode::return_f64 &&
                                        instruction.opcode != Opcode::return_void &&
                                        instruction.opcode != Opcode::return_aggregate &&
                                        instruction.opcode != Opcode::call_void &&
                                        instruction.opcode != Opcode::call_aggregate &&
                                        instruction.opcode != Opcode::call_indirect_void &&
                                        instruction.opcode != Opcode::store_stack_i8 &&
                                        instruction.opcode != Opcode::store_stack_i16 &&
                                        instruction.opcode != Opcode::store_stack_i32 &&
                                        instruction.opcode != Opcode::store_stack_i64 &&
                                        instruction.opcode != Opcode::store_stack_f32 &&
                                        instruction.opcode != Opcode::store_stack_f64 &&
                                        instruction.opcode != Opcode::store_stack_v128 &&
                                        instruction.opcode != Opcode::store_stack_v256 &&
                                        instruction.opcode != Opcode::store_stack_v512 &&
                                        instruction.opcode != Opcode::store_ptr_i8 &&
                                        instruction.opcode != Opcode::store_ptr_i16 &&
                                        instruction.opcode != Opcode::store_ptr_i32 &&
                                        instruction.opcode != Opcode::store_ptr_i64 &&
                                        instruction.opcode != Opcode::store_ptr_f32 &&
                                        instruction.opcode != Opcode::store_ptr_f64 &&
                                        instruction.opcode != Opcode::add_i64_contiguous_inplace &&
                                        instruction.opcode != Opcode::binary_i32_contiguous_inplace &&
                                        instruction.opcode != Opcode::binary_i64_contiguous_inplace &&
                                        instruction.opcode != Opcode::binary_i32_contiguous_map &&
                                        instruction.opcode != Opcode::binary_i64_contiguous_map &&
                                        instruction.opcode != Opcode::binary_i32_contiguous_map2 &&
                                        instruction.opcode != Opcode::binary_i64_contiguous_map2 &&
                                        instruction.opcode != Opcode::binary_i32_contiguous_map3 &&
                                        instruction.opcode != Opcode::binary_i64_contiguous_map3 &&
                                        instruction.opcode != Opcode::binary_i32_contiguous_chain &&
                                        instruction.opcode != Opcode::binary_i64_contiguous_chain &&
                                        instruction.opcode != Opcode::binary_i32_contiguous_dag &&
                                        instruction.opcode != Opcode::binary_i64_contiguous_dag &&
                                        instruction.opcode != Opcode::binary_i32_contiguous_dag_reuse &&
                                        instruction.opcode != Opcode::binary_i64_contiguous_dag_reuse &&
                                        instruction.opcode != Opcode::jump &&
                                        instruction.opcode != Opcode::branch_i1;
                if (has_result) out << 'v' << instruction.result << " = ";
                out << opcode_name(instruction.opcode);
                if (instruction.opcode == Opcode::call_i32 || instruction.opcode == Opcode::call_i64 || instruction.opcode == Opcode::call_void || instruction.opcode == Opcode::call_aggregate || instruction.opcode == Opcode::load_function_address || instruction.opcode == Opcode::load_global_address || instruction.opcode == Opcode::load_tls_address) out << " @" << instruction.symbol;
            if (instruction.opcode == Opcode::load_argument || instruction.opcode == Opcode::load_argument_i64 || instruction.opcode == Opcode::load_argument_f32 || instruction.opcode == Opcode::load_argument_f64) out << ' ' << instruction.argument_index;
                else if (instruction.opcode == Opcode::load_immediate || instruction.opcode == Opcode::load_immediate_i64 || instruction.opcode == Opcode::load_immediate_f32 || instruction.opcode == Opcode::load_immediate_f64 || instruction.opcode == Opcode::load_stack_i8 || instruction.opcode == Opcode::load_stack_i16 || instruction.opcode == Opcode::load_stack_i32 || instruction.opcode == Opcode::load_stack_i64 || instruction.opcode == Opcode::load_stack_f32 || instruction.opcode == Opcode::load_stack_f64 || instruction.opcode == Opcode::load_stack_v128 || instruction.opcode == Opcode::load_stack_v256 || instruction.opcode == Opcode::load_stack_v512) out << ' ' << instruction.immediate;
                else if (instruction.opcode == Opcode::ptr_offset) out << " v" << instruction.inputs[0] << ", " << instruction.immediate;
                else if (instruction.opcode == Opcode::store_stack_i8 || instruction.opcode == Opcode::store_stack_i16 || instruction.opcode == Opcode::store_stack_i32 || instruction.opcode == Opcode::store_stack_i64 || instruction.opcode == Opcode::store_stack_f32 || instruction.opcode == Opcode::store_stack_f64 || instruction.opcode == Opcode::store_stack_v128 || instruction.opcode == Opcode::store_stack_v256 || instruction.opcode == Opcode::store_stack_v512) {
                    if (instruction.symbol == "$storeimm") out << " " << static_cast<std::int32_t>(instruction.argument_index) << ", " << instruction.immediate;
                    else out << " v" << instruction.inputs[0] << ", " << instruction.immediate;
                }
                else if (instruction.opcode == Opcode::store_ptr_i8 || instruction.opcode == Opcode::store_ptr_i16 || instruction.opcode == Opcode::store_ptr_i32 || instruction.opcode == Opcode::store_ptr_i64 || instruction.opcode == Opcode::store_ptr_f32 || instruction.opcode == Opcode::store_ptr_f64) {
                    if (instruction.symbol == "$storeimm") out << " " << static_cast<std::int32_t>(instruction.argument_index) << ", v" << instruction.inputs[0];
                    else out << " v" << instruction.inputs[0] << ", v" << instruction.inputs[1];
                }
                else if (instruction.opcode == Opcode::reduce_add_i32_contiguous ||
                         instruction.opcode == Opcode::reduce_add_i64_contiguous ||
                         instruction.opcode == Opcode::add_i64_contiguous_inplace) {
                    out << " [lanes=" << instruction.immediate << "]";
                    for (const auto input : instruction.inputs) out << " v" << input;
                }
                else if (instruction.opcode == Opcode::binary_i32_contiguous_inplace ||
                         instruction.opcode == Opcode::binary_i64_contiguous_inplace ||
                         instruction.opcode == Opcode::binary_i32_contiguous_map ||
                         instruction.opcode == Opcode::binary_i64_contiguous_map ||
                         instruction.opcode == Opcode::binary_i32_contiguous_map2 ||
                         instruction.opcode == Opcode::binary_i64_contiguous_map2) {
                    out << " [" << opcode_name(static_cast<Opcode>(instruction.argument_index))
                        << ", lanes=" << instruction.immediate << "]";
                    for (const auto input : instruction.inputs) out << " v" << input;
                }
                else if (instruction.opcode == Opcode::binary_i32_contiguous_map3 ||
                         instruction.opcode == Opcode::binary_i64_contiguous_map3) {
                    // Two scalar opcodes packed into one field, low half first.
                    const auto first = static_cast<Opcode>(instruction.argument_index & 0xffffU);
                    const auto second = static_cast<Opcode>((instruction.argument_index >> 16U) & 0xffffU);
                    out << " [" << opcode_name(first) << " -> " << opcode_name(second)
                        << ", lanes=" << instruction.immediate << "]";
                    for (const auto input : instruction.inputs) out << " v" << input;
                }
                else if (instruction.opcode == Opcode::binary_i32_contiguous_chain ||
                         instruction.opcode == Opcode::binary_i64_contiguous_chain) {
                    // `symbol` carries the opcode sequence as little-endian pairs.
                    out << " [";
                    for (std::size_t offset = 0; offset + 1U < instruction.symbol.size(); offset += 2U) {
                        if (offset != 0U) out << " -> ";
                        out << opcode_name(static_cast<Opcode>(packed_token(instruction.symbol, offset)));
                    }
                    out << ", lanes=" << instruction.immediate << "]";
                    for (const auto input : instruction.inputs) out << " v" << input;
                }
                else if (instruction.opcode == Opcode::binary_i32_contiguous_dag_reuse ||
                         instruction.opcode == Opcode::binary_i64_contiguous_dag_reuse) {
                    out << " [reuse-dag nodes=" << (instruction.symbol.size() / 6U)
                        << ", lanes=" << instruction.immediate << "]";
                    for (const auto input : instruction.inputs) out << " v" << input;
                }
                else if (instruction.opcode == Opcode::binary_i32_contiguous_dag ||
                         instruction.opcode == Opcode::binary_i64_contiguous_dag) {
                    // Postfix program: high bit set marks a source operand slot,
                    // anything else is a scalar opcode applied to the stack.
                    out << " [postfix:";
                    for (std::size_t offset = 0; offset + 1U < instruction.symbol.size(); offset += 2U) {
                        const auto token = packed_token(instruction.symbol, offset);
                        if ((token & 0x8000U) != 0U) out << " s" << (token & 0x7fffU);
                        else out << ' ' << opcode_name(static_cast<Opcode>(token));
                    }
                    out << ", lanes=" << instruction.immediate << "]";
                    for (const auto input : instruction.inputs) out << " v" << input;
                }
                else if (instruction.opcode == Opcode::jump) {
                    out << ' ';
                    print_successor(out, instruction.successors[0]);
                } else if (instruction.opcode == Opcode::branch_i1) {
                    if (instruction.immediate != 0 && instruction.inputs.size() == 2U)
                        out << " [fused " << opcode_name(static_cast<Opcode>(instruction.immediate - 1)) << "] v" << instruction.inputs[0] << ", v" << instruction.inputs[1] << ", ";
                    else out << " v" << instruction.inputs[0] << ", ";
                    print_successor(out, instruction.successors[0]);
                    out << ", ";
                    print_successor(out, instruction.successors[1]);
                } else {
                    for (const auto input : instruction.inputs) out << " v" << input;
                    if (instruction.symbol == "$imm") out << ", " << instruction.immediate;
                    else if (instruction.symbol == "$memstack") out << ", [stack " << instruction.immediate << "]";
                }
                out << '\n';
            }
        }
        out << "  }\n";
    }
    out << "}\n";
    return out.str();
}

} // namespace forge::machine
