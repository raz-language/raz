// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <string_view>

namespace forge::ir {

enum class Opcode {
    constant, add, subtract, multiply, divide_signed, divide_unsigned,
    remainder_signed, remainder_unsigned, bit_and, bit_or, bit_xor,
    shift_left, shift_right_signed, shift_right_unsigned,
    compare_equal, compare_not_equal, compare_less_signed, compare_less_unsigned,
    compare_less_equal_signed, compare_less_equal_unsigned,
    copy, load, store, stack_allocate, pointer_offset,
    call, call_indirect, jump, branch, return_, unreachable,
    zero_extend, sign_extend, truncate, bitcast,
    int_to_float_signed, int_to_float_unsigned, float_to_int_signed, float_to_int_unsigned,
    float_extend, float_truncate
};

[[nodiscard]] constexpr std::string_view opcode_name(Opcode opcode) noexcept {
    switch (opcode) {
    case Opcode::constant: return "const";
    case Opcode::add: return "add";
    case Opcode::subtract: return "sub";
    case Opcode::multiply: return "mul";
    case Opcode::divide_signed: return "div.signed";
    case Opcode::divide_unsigned: return "div.unsigned";
    case Opcode::remainder_signed: return "rem.signed";
    case Opcode::remainder_unsigned: return "rem.unsigned";
    case Opcode::bit_and: return "and";
    case Opcode::bit_or: return "or";
    case Opcode::bit_xor: return "xor";
    case Opcode::shift_left: return "shl";
    case Opcode::shift_right_signed: return "shr.signed";
    case Opcode::shift_right_unsigned: return "shr.unsigned";
    case Opcode::compare_equal: return "cmp.eq";
    case Opcode::compare_not_equal: return "cmp.ne";
    case Opcode::compare_less_signed: return "cmp.lt";
    case Opcode::compare_less_unsigned: return "cmp.ult";
    case Opcode::compare_less_equal_signed: return "cmp.le";
    case Opcode::compare_less_equal_unsigned: return "cmp.ule";
    case Opcode::copy: return "copy";
    case Opcode::load: return "load";
    case Opcode::store: return "store";
    case Opcode::stack_allocate: return "stack.alloc";
    case Opcode::pointer_offset: return "ptr.offset";
    case Opcode::call: return "call";
    case Opcode::call_indirect: return "call.indirect";
    case Opcode::jump: return "jump";
    case Opcode::branch: return "branch";
    case Opcode::return_: return "return";
    case Opcode::unreachable: return "unreachable";
    case Opcode::zero_extend: return "zero_extend";
    case Opcode::sign_extend: return "sign_extend";
    case Opcode::truncate: return "truncate";
    case Opcode::bitcast: return "bitcast";
    case Opcode::int_to_float_signed: return "int_to_float.signed";
    case Opcode::int_to_float_unsigned: return "int_to_float.unsigned";
    case Opcode::float_to_int_signed: return "float_to_int.signed";
    case Opcode::float_to_int_unsigned: return "float_to_int.unsigned";
    case Opcode::float_extend: return "float_extend";
    case Opcode::float_truncate: return "float_truncate";
    }
    return "unknown";
}

} // namespace forge::ir
