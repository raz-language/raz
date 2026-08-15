// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/ir/binary.hpp"
#include "forge/ir/opcode.hpp"
#include "forge/ir/parser.hpp"
#include "forge/ir/printer.hpp"
#include "forge/ir/verifier.hpp"
#include "forge/pass/pass.hpp"
#include "forge/transforms/scalar.hpp"
#include <iostream>
#include <stdexcept>

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    try {
        require(forge::ir::opcode_name(forge::ir::Opcode::divide_signed) == "div.signed", "signed division opcode name is not parser-canonical");
        require(forge::ir::opcode_name(forge::ir::Opcode::divide_unsigned) == "div.unsigned", "unsigned division opcode name is not parser-canonical");
        require(forge::ir::opcode_name(forge::ir::Opcode::remainder_signed) == "rem.signed", "signed remainder opcode name is not parser-canonical");
        require(forge::ir::opcode_name(forge::ir::Opcode::remainder_unsigned) == "rem.unsigned", "unsigned remainder opcode name is not parser-canonical");
        require(forge::ir::opcode_name(forge::ir::Opcode::shift_right_signed) == "shr.signed", "signed right-shift opcode name is not parser-canonical");
        require(forge::ir::opcode_name(forge::ir::Opcode::shift_right_unsigned) == "shr.unsigned", "unsigned right-shift opcode name is not parser-canonical");
        require(forge::ir::opcode_name(forge::ir::Opcode::int_to_float_signed) == "int_to_float.signed", "signed int-to-float opcode name is not parser-canonical");
        require(forge::ir::opcode_name(forge::ir::Opcode::int_to_float_unsigned) == "int_to_float.unsigned", "unsigned int-to-float opcode name is not parser-canonical");
        require(forge::ir::opcode_name(forge::ir::Opcode::float_to_int_signed) == "float_to_int.signed", "signed float-to-int opcode name is not parser-canonical");
        require(forge::ir::opcode_name(forge::ir::Opcode::float_to_int_unsigned) == "float_to_int.unsigned", "unsigned float-to-int opcode name is not parser-canonical");
        require(forge::ir::opcode_name(forge::ir::Opcode::float_extend) == "float_extend", "float extend opcode name is not parser-canonical");
        require(forge::ir::opcode_name(forge::ir::Opcode::float_truncate) == "float_truncate", "float truncate opcode name is not parser-canonical");

        constexpr auto source = R"(module @test {
func @sum(%limit: i32) -> i32 {
entry:
  %zero = const i32 0
  %one = const i32 1
  %copied = copy i32 %zero
  %dead = add i32 %zero %one
  jump loop(%copied, %zero)
loop(%index: i32, %total: i32):
  %done = const i1 1
  branch %done, exit(%total), body(%index, %total)
body(%index2: i32, %total2: i32):
  %next = add i32 %index2 %one
  jump loop(%next, %total2)
exit(%result: i32):
  return %result
unused:
  return %zero
}
})";
        auto parsed = forge::ir::parse_module(source);
        for (const auto& diagnostic : parsed.diagnostics) std::cerr << diagnostic.message << '\n';
        require(parsed.ok(), "parser rejected valid control-flow IR");
        auto diagnostics = forge::ir::verify_module(*parsed.module);
        for (const auto& diagnostic : diagnostics) std::cerr << diagnostic.message << '\n';
        require(diagnostics.empty(), "verifier rejected valid control-flow IR");

        forge::pass::PassManager pipeline;
        pipeline.add<forge::transforms::ConstantFoldPass>()
                .add<forge::transforms::CopyPropagationPass>()
                .add<forge::transforms::BranchFoldPass>()
                .add<forge::transforms::SimplifyCFGPass>()
                .add<forge::transforms::DeadCodeEliminationPass>();
        const auto result = pipeline.run(*parsed.module);
        require(result.changed, "optimization pipeline reported no changes");

        const auto text = forge::ir::print_module(*parsed.module);
        require(text.find("unused:") == std::string::npos, "unreachable block survived CFG cleanup");
        require(text.find("%dead") == std::string::npos, "dead SSA operation survived DCE");
        require(text.find("%copied") == std::string::npos, "copy survived propagation");
        require(text.find("branch") == std::string::npos, "constant branch survived folding");
        auto roundtrip = forge::ir::parse_module(text);
        require(roundtrip.ok(), "canonical printer output did not parse");
        require(forge::ir::verify_module(*roundtrip.module).empty(), "round-tripped IR did not verify");


        constexpr auto numeric_casts = R"(module @numeric_casts {
func @casts(%signed: i64, %unsigned: i32, %wide: f64, %single: f32) -> i64 {
entry:
  %a = int_to_float.signed f64 %signed
  %b = int_to_float.unsigned f64 %unsigned
  %c = float_to_int.signed i64 %a
  %d = float_to_int.unsigned i32 %b
  %e = float_truncate f32 %wide
  %f = float_extend f64 %single
  return %c
}
})";
        auto numeric = forge::ir::parse_module(numeric_casts);
        require(numeric.ok(), "numeric cast fixture did not parse");
        require(forge::ir::verify_module(*numeric.module).empty(), "numeric cast fixture did not verify");
        const auto numeric_text = forge::ir::print_module(*numeric.module);
        require(numeric_text.find("int_to_float.signed f64") != std::string::npos, "printer lost signed int-to-float cast");
        require(numeric_text.find("float_to_int.unsigned i32") != std::string::npos, "printer lost unsigned float-to-int cast");
        require(forge::ir::parse_module(numeric_text).ok(), "numeric cast printer output did not round-trip");

        constexpr auto invalid_casts = R"(module @invalid_casts {
func @bad_extend(%value: i64) -> i32 {
entry:
  %bad = zero_extend i32 %value
  return %bad
}
func @bad_truncate(%value: i32) -> i64 {
entry:
  %bad = truncate i64 %value
  return %bad
}
})";
        auto invalid = forge::ir::parse_module(invalid_casts);
        require(invalid.ok(), "invalid-cast fixture did not parse");
        const auto invalid_diagnostics = forge::ir::verify_module(*invalid.module);
        require(invalid_diagnostics.size() >= 2, "verifier accepted invalid integer cast widths");

        constexpr auto aligned_source = R"(module @aligned {
func @memory() -> i64 {
entry:
  %buffer = stack.alloc ptr 16 align 16
  %value = const i64 42
  store i64 %value %buffer align 8
  %loaded = load i64 %buffer align 8
  return %loaded
}
})";
        auto aligned = forge::ir::parse_module(aligned_source);
        require(aligned.ok(), "aligned-memory fixture did not parse");
        require(forge::ir::verify_module(*aligned.module).empty(), "aligned-memory fixture did not verify");
        const auto aligned_text = forge::ir::print_module(*aligned.module);
        require(aligned_text.find("stack.alloc ptr 16 align 16") != std::string::npos, "printer lost stack alignment");
        require(aligned_text.find("load i64 %buffer align 8") != std::string::npos, "printer lost load alignment");

        constexpr auto aggregate_globals = R"(module @aggregate_globals {
constant @message: i8[8] align 8 = "Forge!\0\0"
global @scratch: i8[32] align 16 = zero
func @address() -> ptr {
entry:
  %value = global.address ptr @message
  return %value
}
})";
        auto aggregates = forge::ir::parse_module(aggregate_globals);
        require(aggregates.ok(), "aggregate-global fixture did not parse");
        require(forge::ir::verify_module(*aggregates.module).empty(), "aggregate-global fixture did not verify");
        const auto aggregate_text = forge::ir::print_module(*aggregates.module);
        require(aggregate_text.find("i8[8] align 8 = \"Forge!\\0\\0\"") != std::string::npos, "printer lost string constant");
        require(aggregate_text.find("i8[32] align 16 = zero") != std::string::npos, "printer lost zero-filled global");
        auto aggregate_roundtrip = forge::ir::parse_module(aggregate_text);
        require(aggregate_roundtrip.ok(), "aggregate-global printer output did not parse");


        constexpr auto aggregate_return_source = R"(module @aggregate_returns {
struct @Record { value: i64 }
global @record: struct @Record = { value: 42 }
func @borrow() -> owned struct @Record {
entry:
  %record = global.address ptr @record
  return %record
}
})";
        auto aggregate_return = forge::ir::parse_module(aggregate_return_source);
        require(aggregate_return.ok(), "aggregate-return fixture did not parse");
        require(forge::ir::verify_module(*aggregate_return.module).empty(), "aggregate-return fixture did not verify");
        const auto aggregate_return_text = forge::ir::print_module(*aggregate_return.module);
        require(aggregate_return_text.find("-> owned struct @Record") != std::string::npos, "printer lost owned aggregate return identity");
        require(aggregate_return.module->functions().front().return_owned, "parser lost aggregate return ownership");

        constexpr auto invalid_aggregate_return = R"(module @invalid_aggregate_return {
func @bad() -> struct @Missing {
entry:
  %value = const i64 0
  return %value
}
})";
        auto bad_aggregate_return = forge::ir::parse_module(invalid_aggregate_return);
        require(bad_aggregate_return.ok(), "invalid aggregate-return fixture did not parse");
        require(!forge::ir::verify_module(*bad_aggregate_return.module).empty(), "verifier accepted invalid aggregate return");

        constexpr auto named_struct = R"(module @records {
struct @Record { tag: i8, value: i64, code: i16 }
func @field(%base: ptr) -> ptr {
entry:
  %value = struct.field.address ptr %base @Record 1
  %named = struct.field.name.address ptr %base @Record value
  %size = sizeof.struct i64 @Record
  %alignment = alignof.struct i64 @Record
  return %named
}
})";
        auto record = forge::ir::parse_module(named_struct);
        require(record.ok(), "named-structure fixture did not parse");
        require(forge::ir::verify_module(*record.module).empty(), "named-structure fixture did not verify");
        const auto record_text = forge::ir::print_module(*record.module);
        require(record_text.find("struct @Record { tag: i8, value: i64, code: i16 }") != std::string::npos,
                "printer lost named structure declaration");
        require(record_text.find("struct.field.address ptr %base @Record 1") != std::string::npos,
                "printer lost typed field address");
        require(record_text.find("struct.field.name.address ptr %base @Record value") != std::string::npos,
                "printer lost named field address");
        require(record_text.find("sizeof.struct i64 @Record") != std::string::npos &&
                record_text.find("alignof.struct i64 @Record") != std::string::npos,
                "printer lost structure layout constants");
        auto record_roundtrip = forge::ir::parse_module(record_text);
        require(record_roundtrip.ok() && forge::ir::verify_module(*record_roundtrip.module).empty(),
                "named structure round-trip failed");

        constexpr auto invalid_struct = R"(module @bad_record {
struct @Broken { value: void }
func @field(%base: ptr) -> ptr {
entry:
  %value = struct.field.address ptr %base @Missing 9
  %named = struct.field.name.address ptr %base @Broken missing
  %size = sizeof.struct i64 @Missing
  return %value
}
})";
        auto bad_record = forge::ir::parse_module(invalid_struct);
        require(bad_record.ok(), "invalid-structure fixture did not parse");
        require(forge::ir::verify_module(*bad_record.module).size() >= 4,
                "verifier accepted invalid structure declarations or field addressing");

        constexpr auto named_array_source = R"(module @named_array {
array @Words = i16[8]
func @element(%base: ptr) -> ptr {
entry:
  %address = array.element.address ptr %base @Words 3
  %size = sizeof.array i64 @Words
  %alignment = alignof.array i64 @Words
  return %address
}
})";
        auto named_array = forge::ir::parse_module(named_array_source);
        require(named_array.ok(), "named-array fixture did not parse");
        require(forge::ir::verify_module(*named_array.module).empty(), "named-array fixture did not verify");
        const auto named_array_text = forge::ir::print_module(*named_array.module);
        require(named_array_text.find("array @Words = i16[8]") != std::string::npos, "printer lost named array declaration");
        require(named_array_text.find("array.element.address ptr %base @Words 3") != std::string::npos, "printer lost array element address");
        require(named_array_text.find("sizeof.array i64 @Words") != std::string::npos &&
                named_array_text.find("alignof.array i64 @Words") != std::string::npos, "printer lost array layout constants");
        auto named_array_roundtrip = forge::ir::parse_module(named_array_text);
        require(named_array_roundtrip.ok() && forge::ir::verify_module(*named_array_roundtrip.module).empty(),
                "named array round-trip failed");

        constexpr auto aggregate_element_array_source = R"(module @aggregate_element_array {
struct @Pair { left: i64, right: i64 }
array @Pairs = struct @Pair [2]
func @identity(%values: array @Pairs) -> array @Pairs {
entry:
  %copy = copy ptr %values
  return %copy
}
})";
        auto aggregate_element_array = forge::ir::parse_module(aggregate_element_array_source);
        require(aggregate_element_array.ok(), "aggregate-element array fixture did not parse");
        require(forge::ir::verify_module(*aggregate_element_array.module).empty(), "aggregate-element array fixture did not verify");
        const auto aggregate_element_array_text = forge::ir::print_module(*aggregate_element_array.module);
        require(aggregate_element_array_text.find("array @Pairs = struct @Pair [2]") != std::string::npos,
                "printer lost aggregate-element array declaration");
        auto aggregate_element_array_roundtrip = forge::ir::parse_module(aggregate_element_array_text);
        require(aggregate_element_array_roundtrip.ok() && forge::ir::verify_module(*aggregate_element_array_roundtrip.module).empty(),
                "aggregate-element array round-trip failed");
        const auto aggregate_element_array_binary = forge::ir::write_binary(*aggregate_element_array.module);
        require(aggregate_element_array_binary.ok(), "aggregate-element array binary encoding failed");
        const auto aggregate_element_array_decoded = forge::ir::read_binary(aggregate_element_array_binary.bytes);
        require(aggregate_element_array_decoded.ok() &&
                forge::ir::print_module(aggregate_element_array_decoded.module) == aggregate_element_array_text,
                "aggregate-element array binary round-trip failed");

        constexpr auto dynamic_aggregate_element_array_source = R"(module @dynamic_aggregate_element_array {
struct @Pair { left: i64, right: i64 }
array @Pairs = struct @Pair [2]
func @read_left(%values: array @Pairs, %index: i64) -> i64 {
entry:
  %element = array.element.address ptr %values @Pairs %index
  %field = struct.field.address ptr %element @Pair 0
  %value = load i64 %field align 8
  return %value
}
})";
        auto dynamic_aggregate_element_array = forge::ir::parse_module(dynamic_aggregate_element_array_source);
        require(dynamic_aggregate_element_array.ok(), "dynamic aggregate-element array fixture did not parse");
        require(forge::ir::verify_module(*dynamic_aggregate_element_array.module).empty(),
                "dynamic aggregate-element array fixture did not verify");

        constexpr auto typed_initializers = R"(module @typed_initializers {
array @Words = i16[4]
struct @Inner { value: i64, words: array @Words }
struct @Outer { tag: i8, inner: struct @Inner, tail: i16 }
constant @words: array @Words = [1, 258, 65535, 4]
global @record: struct @Outer = { tail: 4660, tag: 127, inner: { words: [5, 6, 7, 8], value: 72623859790382856 } }
})";
        auto initialized = forge::ir::parse_module(typed_initializers);
        require(initialized.ok(), "typed aggregate initializer fixture did not parse");
        require(forge::ir::verify_module(*initialized.module).empty(), "typed aggregate initializer fixture did not verify");
        require(initialized.module->globals().size() == 2, "typed aggregate globals are missing");
        const auto& word_bytes = initialized.module->globals()[0].bytes;
        require(word_bytes.size() == 8 && word_bytes[0] == 1 && word_bytes[2] == 2 && word_bytes[3] == 1 &&
                word_bytes[4] == 0xff && word_bytes[5] == 0xff, "named-array initializer encoded incorrect bytes");
        const auto& record_bytes = initialized.module->globals()[1].bytes;
        require(record_bytes.size() == 32 && record_bytes[0] == 127 && record_bytes[8] == 8 && record_bytes[9] == 7 &&
                record_bytes[16] == 5 && record_bytes[18] == 6 && record_bytes[24] == 0x34 && record_bytes[25] == 0x12,
                "nested structure initializer encoded incorrect layout bytes");
        const auto initialized_text = forge::ir::print_module(*initialized.module);
        auto initialized_roundtrip = forge::ir::parse_module(initialized_text);
        require(initialized_roundtrip.ok() && forge::ir::verify_module(*initialized_roundtrip.module).empty(),
                "typed aggregate initializer canonical byte round-trip failed");

        constexpr auto invalid_initializers = R"(module @bad_initializers {
array @Words = i16[2]
struct @Pair { left: i8, right: i8 }
constant @few: array @Words = [1]
global @missing: struct @Pair = { left: 1 }
})";
        auto bad_initializers = forge::ir::parse_module(invalid_initializers);
        require(!bad_initializers.ok(), "parser accepted incomplete typed aggregate initializers");

        constexpr auto invalid_array = R"(module @bad_array {
array @Broken = void[4]
array @Empty = i8[0]
func @bad(%base: ptr) -> ptr {
entry:
  %address = array.element.address ptr %base @Missing 9
  %size = sizeof.array i64 @Missing
  return %address
}
})";
        auto bad_array = forge::ir::parse_module(invalid_array);
        require(bad_array.ok(), "invalid-array fixture did not parse");
        require(forge::ir::verify_module(*bad_array.module).size() >= 4, "verifier accepted invalid named arrays");

        constexpr auto aggregate_parameters = R"(module @aggregate_parameters {
array @Words = i16[4]
struct @Record { value: i64, words: array @Words }
func @read(%record: struct @Record, %words: array @Words) -> i64 {
entry:
  %value_address = struct.field.name.address ptr %record @Record value
  %value = load i64 %value_address align 8
  return %value
}
})";
        auto aggregate_parameter_module = forge::ir::parse_module(aggregate_parameters);
        require(aggregate_parameter_module.ok(), "aggregate parameter fixture did not parse");
        require(forge::ir::verify_module(*aggregate_parameter_module.module).empty(), "aggregate parameter fixture did not verify");
        const auto aggregate_parameter_text = forge::ir::print_module(*aggregate_parameter_module.module);
        require(aggregate_parameter_text.find("%record: struct @Record") != std::string::npos &&
                aggregate_parameter_text.find("%words: array @Words") != std::string::npos,
                "printer lost typed aggregate parameters");

        constexpr auto aggregate_copy_return_source = R"(module @aggregate_copy_return {
struct @Record { value: i64 }
func @identity(%record: owned struct @Record) -> owned struct @Record {
entry:
  %copy = copy ptr %record
  return %copy
}
})";
        auto aggregate_copy_return = forge::ir::parse_module(aggregate_copy_return_source);
        require(aggregate_copy_return.ok(), "aggregate copy-return fixture did not parse");
        require(forge::ir::verify_module(*aggregate_copy_return.module).empty(),
                "copy lost aggregate identity before aggregate return");

        constexpr auto invalid_aggregate_parameter = R"(module @bad_aggregate_parameter {
struct @Record { value: i64 }
func @bad(%value: struct @Missing) -> void {
entry:
  return
}
})";
        auto bad_aggregate_parameter = forge::ir::parse_module(invalid_aggregate_parameter);
        require(bad_aggregate_parameter.ok(), "invalid aggregate parameter fixture did not parse");
        require(!forge::ir::verify_module(*bad_aggregate_parameter.module).empty(), "verifier accepted unknown aggregate parameter type");

        constexpr auto aggregate_move_source = R"(module @aggregate_move {
struct @Record { value: i64 }
func @good(%record: struct @Record) -> i64 {
entry:
  %moved = aggregate.move.struct ptr %record @Record
  %field = struct.field.name.address ptr %moved @Record value
  %result = load i64 %field align 8
  return %result
}
})";
        auto aggregate_move = forge::ir::parse_module(aggregate_move_source);
        require(aggregate_move.ok(), "aggregate move fixture did not parse");
        require(forge::ir::verify_module(*aggregate_move.module).empty(), "valid aggregate move did not verify");
        const auto aggregate_move_text = forge::ir::print_module(*aggregate_move.module);
        require(aggregate_move_text.find("aggregate.move.struct ptr %record @Record") != std::string::npos,
                "printer lost aggregate move");

        constexpr auto invalid_aggregate_move = R"(module @invalid_aggregate_move {
struct @Record { value: i64 }
func @bad(%record: struct @Record) -> i64 {
entry:
  %moved = aggregate.move.struct ptr %record @Record
  %field = struct.field.name.address ptr %record @Record value
  %again = aggregate.move.struct ptr %record @Record
  %result = load i64 %field align 8
  return %result
}
})";
        auto bad_aggregate_move = forge::ir::parse_module(invalid_aggregate_move);
        require(bad_aggregate_move.ok(), "invalid aggregate move fixture did not parse");
        require(forge::ir::verify_module(*bad_aggregate_move.module).size() >= 2,
                "verifier accepted use-after-move or double move");

        constexpr auto aggregate_end_source = R"(module @aggregate_end {
struct @Record { value: i64 }
func @good(%record: struct @Record) -> void {
entry:
  aggregate.end.struct void %record @Record
  return
}
})";
        auto aggregate_end = forge::ir::parse_module(aggregate_end_source);
        require(aggregate_end.ok(), "aggregate lifetime-end fixture did not parse");
        require(forge::ir::verify_module(*aggregate_end.module).empty(), "valid aggregate lifetime end did not verify");
        const auto aggregate_end_text = forge::ir::print_module(*aggregate_end.module);
        require(aggregate_end_text.find("aggregate.end.struct void %record @Record") != std::string::npos,
                "printer lost aggregate lifetime end");

        constexpr auto invalid_aggregate_end = R"(module @invalid_aggregate_end {
struct @Record { value: i64 }
func @bad(%record: struct @Record) -> i64 {
entry:
  aggregate.end.struct void %record @Record
  %field = struct.field.name.address ptr %record @Record value
  %result = load i64 %field align 8
  return %result
}
})";
        auto bad_aggregate_end = forge::ir::parse_module(invalid_aggregate_end);
        require(bad_aggregate_end.ok(), "invalid aggregate lifetime-end fixture did not parse");
        require(!forge::ir::verify_module(*bad_aggregate_end.module).empty(),
                "verifier accepted use after aggregate lifetime end");


        constexpr auto aggregate_borrow_source = R"(module @aggregate_borrows {
struct @Record { value: i64 }
func @good(%record: struct @Record) -> i64 {
entry:
  %first = aggregate.borrow.struct ptr %record @Record
  %second = aggregate.borrow.struct ptr %record @Record
  %field = struct.field.name.address ptr %first @Record value
  %result = load i64 %field align 8
  aggregate.borrow.end.struct void %second @Record
  aggregate.borrow.end.struct void %first @Record
  return %result
}
})";
        auto aggregate_borrow = forge::ir::parse_module(aggregate_borrow_source);
        require(aggregate_borrow.ok(), "aggregate borrow fixture did not parse");
        require(forge::ir::verify_module(*aggregate_borrow.module).empty(), "valid immutable aggregate borrows did not verify");
        const auto aggregate_borrow_text = forge::ir::print_module(*aggregate_borrow.module);
        require(aggregate_borrow_text.find("aggregate.borrow.struct ptr %record @Record") != std::string::npos &&
                aggregate_borrow_text.find("aggregate.borrow.end.struct void %first @Record") != std::string::npos,
                "printer lost aggregate borrow operations");

        constexpr auto invalid_borrow_conflict = R"(module @bad_borrow_conflict {
struct @Record { value: i64 }
func @bad(%record: struct @Record) -> void {
entry:
  %shared = aggregate.borrow.struct ptr %record @Record
  %exclusive = aggregate.borrow.mut.struct ptr %record @Record
  aggregate.borrow.end.struct void %exclusive @Record
  aggregate.borrow.end.struct void %shared @Record
  return
}
})";
        auto bad_borrow_conflict = forge::ir::parse_module(invalid_borrow_conflict);
        require(bad_borrow_conflict.ok(), "borrow conflict fixture did not parse");
        require(!forge::ir::verify_module(*bad_borrow_conflict.module).empty(), "verifier accepted conflicting aggregate borrows");

        constexpr auto invalid_move_while_borrowed = R"(module @bad_move_while_borrowed {
struct @Record { value: i64 }
func @bad(%record: struct @Record) -> void {
entry:
  %borrow = aggregate.borrow.struct ptr %record @Record
  %moved = aggregate.move.struct ptr %record @Record
  aggregate.borrow.end.struct void %borrow @Record
  aggregate.end.struct void %moved @Record
  return
}
})";
        auto bad_move_while_borrowed = forge::ir::parse_module(invalid_move_while_borrowed);
        require(bad_move_while_borrowed.ok(), "move-while-borrowed fixture did not parse");
        require(!forge::ir::verify_module(*bad_move_while_borrowed.module).empty(), "verifier accepted move while aggregate was borrowed");
        constexpr auto moveonly_source = R"(module @moveonly_types {
moveonly struct @Token { value: i64 }
moveonly array @Bytes = i8[8]
func @consume(%token: struct @Token) -> i64 {
entry:
  %moved = aggregate.move.struct ptr %token @Token
  %field = struct.field.name.address ptr %moved @Token value
  %result = load i64 %field align 8
  aggregate.end.struct void %moved @Token
  return %result
}
})";
        auto moveonly = forge::ir::parse_module(moveonly_source);
        require(moveonly.ok(), "move-only aggregate fixture did not parse");
        require(forge::ir::verify_module(*moveonly.module).empty(), "valid move-only aggregate fixture did not verify");
        const auto moveonly_text = forge::ir::print_module(*moveonly.module);
        require(moveonly_text.find("moveonly struct @Token") != std::string::npos &&
                moveonly_text.find("moveonly array @Bytes") != std::string::npos,
                "printer lost move-only aggregate declarations");

        constexpr auto invalid_moveonly_copy = R"(module @bad_moveonly_copy {
moveonly struct @Token { value: i64 }
func @bad(%source: struct @Token, %destination: struct @Token) -> void {
entry:
  aggregate.copy.struct void %destination %source @Token
  return
}
})";
        auto bad_moveonly_copy = forge::ir::parse_module(invalid_moveonly_copy);
        require(bad_moveonly_copy.ok(), "move-only copy rejection fixture did not parse");
        require(!forge::ir::verify_module(*bad_moveonly_copy.module).empty(),
                "verifier accepted copying a move-only aggregate");

        constexpr auto invalid_moveonly_owned = R"(module @bad_moveonly_owned {
moveonly struct @Token { value: i64 }
func @bad(%token: owned struct @Token) -> owned struct @Token {
entry:
  return %token
}
})";
        auto bad_moveonly_owned = forge::ir::parse_module(invalid_moveonly_owned);
        require(bad_moveonly_owned.ok(), "move-only owned-boundary fixture did not parse");
        require(forge::ir::verify_module(*bad_moveonly_owned.module).size() >= 2,
                "verifier accepted copying move-only aggregate at owned boundaries");

        constexpr auto invalid_moveonly_field = R"(module @bad_moveonly_field {
moveonly struct @Token { value: i64 }
struct @Envelope { token: struct @Token }
func @noop() -> void { entry: return }
})";
        auto bad_moveonly_field = forge::ir::parse_module(invalid_moveonly_field);
        require(bad_moveonly_field.ok(), "move-only field propagation fixture did not parse");
        require(!forge::ir::verify_module(*bad_moveonly_field.module).empty(),
                "verifier accepted a copyable structure containing a move-only field");

        constexpr auto invalid_immutable_borrow_store = R"(module @bad_immutable_borrow_store {
struct @Record { value: i64 }
func @bad(%record: struct @Record, %replacement: i64) -> void {
entry:
  %borrow = aggregate.borrow.struct ptr %record @Record
  %field = struct.field.name.address ptr %borrow @Record value
  store i64 %replacement %field align 8
  aggregate.borrow.end.struct void %borrow @Record
  return
}
})";
        auto bad_immutable_borrow_store = forge::ir::parse_module(invalid_immutable_borrow_store);
        require(bad_immutable_borrow_store.ok(), "immutable-borrow store fixture did not parse");
        require(!forge::ir::verify_module(*bad_immutable_borrow_store.module).empty(),
                "verifier accepted a store through an immutable aggregate borrow");

        constexpr auto valid_mutable_borrow_store = R"(module @mutable_borrow_store {
struct @Record { value: i64 }
func @good(%record: struct @Record, %replacement: i64) -> i64 {
entry:
  %borrow = aggregate.borrow.mut.struct ptr %record @Record
  %field = struct.field.name.address ptr %borrow @Record value
  store i64 %replacement %field align 8
  %result = load i64 %field align 8
  aggregate.borrow.end.struct void %borrow @Record
  return %result
}
})";
        auto mutable_borrow_store = forge::ir::parse_module(valid_mutable_borrow_store);
        require(mutable_borrow_store.ok(), "mutable-borrow store fixture did not parse");
        require(forge::ir::verify_module(*mutable_borrow_store.module).empty(),
                "verifier rejected a store through a mutable aggregate borrow");

        constexpr auto invalid_immutable_bulk_mutation = R"(module @bad_immutable_bulk_mutation {
array @Bytes = i8[8]
func @bad(%bytes: array @Bytes, %value: i8) -> void {
entry:
  %borrow = aggregate.borrow.array ptr %bytes @Bytes
  memory.set void %borrow %value 8
  aggregate.borrow.end.array void %borrow @Bytes
  return
}
})";
        auto bad_immutable_bulk_mutation = forge::ir::parse_module(invalid_immutable_bulk_mutation);
        require(bad_immutable_bulk_mutation.ok(), "immutable bulk-mutation fixture did not parse");
        require(!forge::ir::verify_module(*bad_immutable_bulk_mutation.module).empty(),
                "verifier accepted memory.set through an immutable borrow");

        constexpr auto invalid_derived_after_borrow_end = R"(module @bad_derived_after_end {
struct @Record { value: i64 }
func @bad(%record: struct @Record) -> i64 {
entry:
  %borrow = aggregate.borrow.struct ptr %record @Record
  %field = struct.field.name.address ptr %borrow @Record value
  aggregate.borrow.end.struct void %borrow @Record
  %result = load i64 %field align 8
  return %result
}
})";
        auto bad_derived_after_borrow_end = forge::ir::parse_module(invalid_derived_after_borrow_end);
        require(bad_derived_after_borrow_end.ok(), "derived-after-borrow-end fixture did not parse");
        require(!forge::ir::verify_module(*bad_derived_after_borrow_end.module).empty(),
                "verifier accepted a derived pointer after its borrow ended");

        constexpr auto borrowed_parameter_source = R"(module @borrowed_parameters {
struct @Record { value: i64 }
func @read(%record: borrow struct @Record) -> i64 {
entry:
  %field = struct.field.name.address ptr %record @Record value
  %value = load i64 %field align 8
  return %value
}
func @write(%record: borrow mut struct @Record, %value: i64) -> void {
entry:
  %field = struct.field.name.address ptr %record @Record value
  store i64 %value %field align 8
  return
}
})";
        auto borrowed_parameters = forge::ir::parse_module(borrowed_parameter_source);
        require(borrowed_parameters.ok(), "borrowed parameter fixture did not parse");
        require(forge::ir::verify_module(*borrowed_parameters.module).empty(), "valid borrowed parameters did not verify");
        const auto borrowed_parameter_text = forge::ir::print_module(*borrowed_parameters.module);
        require(borrowed_parameter_text.find("%record: borrow struct @Record") != std::string::npos &&
                borrowed_parameter_text.find("%record: borrow mut struct @Record") != std::string::npos,
                "printer lost borrowed parameter modes");

        constexpr auto invalid_immutable_parameter_write = R"(module @bad_immutable_parameter_write {
struct @Record { value: i64 }
func @bad(%record: borrow struct @Record, %value: i64) -> void {
entry:
  %field = struct.field.name.address ptr %record @Record value
  store i64 %value %field align 8
  return
}
})";
        auto bad_immutable_parameter_write = forge::ir::parse_module(invalid_immutable_parameter_write);
        require(bad_immutable_parameter_write.ok(), "immutable borrowed parameter fixture did not parse");
        require(!forge::ir::verify_module(*bad_immutable_parameter_write.module).empty(),
                "verifier accepted mutation through immutable borrowed parameter");

        constexpr auto valid_mutable_reborrow = R"(module @mutable_reborrow {
struct @Record { value: i64 }
func @good(%record: borrow mut struct @Record, %value: i64) -> i64 {
entry:
  %child = aggregate.borrow.mut.struct ptr %record @Record
  %field = struct.field.name.address ptr %child @Record value
  store i64 %value %field align 8
  aggregate.borrow.end.struct void %child @Record
  %parent_field = struct.field.name.address ptr %record @Record value
  %result = load i64 %parent_field align 8
  return %result
}
})";
        auto mutable_reborrow = forge::ir::parse_module(valid_mutable_reborrow);
        require(mutable_reborrow.ok(), "mutable reborrow fixture did not parse");
        require(forge::ir::verify_module(*mutable_reborrow.module).empty(), "valid mutable reborrow did not verify");

        constexpr auto invalid_mutable_call = R"(module @bad_mutable_call {
struct @Record { value: i64 }
func @mutate(%record: borrow mut struct @Record) -> void {
entry:
  return
}
func @bad(%record: struct @Record) -> void {
entry:
  %shared = aggregate.borrow.struct ptr %record @Record
  call void @mutate(%shared)
  aggregate.borrow.end.struct void %shared @Record
  return
}
})";
        auto bad_mutable_call = forge::ir::parse_module(invalid_mutable_call);
        require(bad_mutable_call.ok(), "mutable-call conflict fixture did not parse");
        require(!forge::ir::verify_module(*bad_mutable_call.module).empty(),
                "verifier accepted immutable borrow for mutable borrowed call parameter");

        constexpr auto borrowed_returns = R"(module @borrowed_returns {
struct @Record { value: i64 }
func @identity(%record: borrow struct @Record) -> borrow struct @Record from 0 {
entry:
  return %record
}
func @identity_mut(%record: borrow mut struct @Record) -> borrow mut struct @Record from 0 {
entry:
  %field = struct.field.name.address ptr %record @Record value
  return %record
}
func @read(%record: struct @Record) -> i64 {
entry:
  %loan = call ptr @identity(%record)
  %field = struct.field.name.address ptr %loan @Record value
  %value = load i64 %field align 8
  return %value
}
})";
        auto borrowed_return_module = forge::ir::parse_module(borrowed_returns);
        require(borrowed_return_module.ok(), "borrowed-return fixture did not parse");
        require(forge::ir::verify_module(*borrowed_return_module.module).empty(), "borrowed-return fixture did not verify");
        const auto borrowed_return_text = forge::ir::print_module(*borrowed_return_module.module);
        require(borrowed_return_text.find("-> borrow struct @Record from 0") != std::string::npos,
                "printer lost immutable borrowed return metadata");
        require(borrowed_return_text.find("-> borrow mut struct @Record from 0") != std::string::npos,
                "printer lost mutable borrowed return metadata");

        constexpr auto conditional_borrowed_return = R"(module @conditional_borrowed_return {
struct @Record { value: i64 }
func @select(%condition: i1, %record: borrow struct @Record) -> borrow struct @Record from 1 {
entry:
  branch %condition, left(%record), right(%record)
left(%left_record: struct @Record):
  jump join(%left_record)
right(%right_record: struct @Record):
  jump join(%right_record)
join(%selected: struct @Record):
  return %selected
}
})";
        auto conditional_return = forge::ir::parse_module(conditional_borrowed_return);
        require(conditional_return.ok(), "conditional borrowed-return fixture did not parse");
        require(forge::ir::verify_module(*conditional_return.module).empty(),
                "valid conditional borrowed return did not verify");

        constexpr auto invalid_mixed_borrowed_return = R"(module @invalid_mixed_borrowed_return {
struct @Record { value: i64 }
func @select(%condition: i1, %first: borrow struct @Record, %second: borrow struct @Record) -> borrow struct @Record from 1 {
entry:
  branch %condition, join(%first), join(%second)
join(%selected: struct @Record):
  return %selected
}
})";
        auto mixed_return = forge::ir::parse_module(invalid_mixed_borrowed_return);
        require(mixed_return.ok(), "mixed borrowed-return fixture did not parse");
        require(!forge::ir::verify_module(*mixed_return.module).empty(),
                "verifier accepted a borrowed return joined from unrelated source loans");

        constexpr auto invalid_borrowed_escape = R"(module @invalid_borrowed_escape {
struct @Record { value: i64 }
func @bad(%input: borrow struct @Record) -> borrow struct @Record from 0 {
entry:
  %local = stack.alloc.struct ptr @Record
  return %local
}
})";
        auto bad_borrowed_escape = forge::ir::parse_module(invalid_borrowed_escape);
        require(bad_borrowed_escape.ok(), "invalid borrowed-return escape fixture did not parse");
        require(!forge::ir::verify_module(*bad_borrowed_escape.module).empty(),
                "verifier accepted a borrowed return escaping local stack storage");

        constexpr auto invalid_mutable_borrowed_return = R"(module @invalid_mutable_borrowed_return {
struct @Record { value: i64 }
func @bad(%input: borrow struct @Record) -> borrow mut struct @Record from 0 {
entry:
  return %input
}
})";
        auto bad_mutable_borrowed_return = forge::ir::parse_module(invalid_mutable_borrowed_return);
        require(bad_mutable_borrowed_return.ok(), "invalid mutable borrowed-return fixture did not parse");
        require(!forge::ir::verify_module(*bad_mutable_borrowed_return.module).empty(),
                "verifier accepted mutable borrowed return from immutable source");


        constexpr auto deep_borrowed_return = R"(module @deep_borrowed_return {
struct @Record { value: i64 }
func @identity(%record: borrow struct @Record) -> borrow struct @Record from 0 {
entry:
  return %record
}
func @forward(%record: borrow struct @Record) -> borrow struct @Record from 0 {
entry:
  %loan = call ptr @identity(%record)
  jump loop(%loan)
loop(%loop_loan: struct @Record):
  return %loop_loan
}
})";
        auto deep_return = forge::ir::parse_module(deep_borrowed_return);
        require(deep_return.ok(), "deep borrowed-return fixture did not parse");
        require(forge::ir::verify_module(*deep_return.module).empty(),
                "multi-level loop-carried borrowed return did not verify");


        constexpr auto typed_indirect = R"(module @typed_indirect {
struct @Record { value: i64 }
func @identity(%record: borrow struct @Record) -> borrow struct @Record from 0 {
entry:
  return %record
}
func @dispatch(%record: borrow struct @Record) -> borrow struct @Record from 0 {
entry:
  %target = func.address ptr @identity
  %loan = call.indirect ptr %target as @identity(%record)
  return %loan
}
})";
        auto typed_indirect_module = forge::ir::parse_module(typed_indirect);
        require(typed_indirect_module.ok(), "typed indirect-call fixture did not parse");
        require(forge::ir::verify_module(*typed_indirect_module.module).empty(), "typed indirect-call fixture did not verify");
        const auto typed_indirect_text = forge::ir::print_module(*typed_indirect_module.module);
        require(typed_indirect_text.find("call.indirect ptr %target as @identity(%record)") != std::string::npos,
                "printer lost typed indirect-call signature");

        constexpr auto independent_signature = R"(module @independent_signature {
struct @Record { value: i64 }
signature @BorrowIdentity(%record: borrow struct @Record) -> borrow struct @Record from 0
func @identity(%record: borrow struct @Record) -> borrow struct @Record from 0 {
entry:
  return %record
}
func @dispatch(%record: borrow struct @Record) -> borrow struct @Record from 0 {
entry:
  %target = func.address ptr @identity as @BorrowIdentity
  %loan = call.indirect ptr %target as @BorrowIdentity(%record)
  return %loan
}
})";
        auto independent_signature_module = forge::ir::parse_module(independent_signature);
        require(independent_signature_module.ok(), "independent signature fixture did not parse");
        require(forge::ir::verify_module(*independent_signature_module.module).empty(), "independent signature fixture did not verify");
        const auto independent_signature_text = forge::ir::print_module(*independent_signature_module.module);
        require(independent_signature_text.find("signature @BorrowIdentity") != std::string::npos,
                "printer lost independent signature declaration");
        require(independent_signature_text.find("func.address ptr @identity as @BorrowIdentity") != std::string::npos,
                "printer lost function-address signature assertion");

        constexpr auto incompatible_signature_address = R"(module @incompatible_signature_address {
signature @Narrow(%value: i32) -> i32
func @wide(%value: i64) -> i64 { entry: return %value }
func @bad() -> ptr {
entry:
  %target = func.address ptr @wide as @Narrow
  return %target
}
})";
        auto incompatible_signature_module = forge::ir::parse_module(incompatible_signature_address);
        require(incompatible_signature_module.ok(), "incompatible signature-address fixture did not parse");
        require(!forge::ir::verify_module(*incompatible_signature_module.module).empty(),
                "verifier accepted incompatible func.address signature assertion");

        constexpr auto direct_signature_call = R"(module @direct_signature_call {
signature @Unary(%value: i64) -> i64
func @bad(%value: i64) -> i64 {
entry:
  %result = call i64 @Unary(%value)
  return %result
}
})";
        auto direct_signature_module = forge::ir::parse_module(direct_signature_call);
        require(direct_signature_module.ok(), "direct signature-call fixture did not parse");
        require(!forge::ir::verify_module(*direct_signature_module.module).empty(),
                "verifier accepted a direct call to a signature declaration");

        constexpr auto invalid_indirect = R"(module @invalid_indirect {
func @wide(%value: i64) -> i64 { entry: return %value }
func @narrow(%value: i32) -> i32 { entry: return %value }
func @bad(%value: i64) -> i64 {
entry:
  %target = func.address ptr @wide
  %result = call.indirect i64 %target as @narrow(%value)
  return %result
}
})";
        auto invalid_indirect_module = forge::ir::parse_module(invalid_indirect);
        require(invalid_indirect_module.ok(), "invalid typed indirect-call fixture did not parse");
        require(!forge::ir::verify_module(*invalid_indirect_module.module).empty(),
                "verifier accepted an incompatible indirect-call signature");

        constexpr auto invalid_alignment = R"(module @bad_alignment {
func @bad() -> i64 {
entry:
  %buffer = stack.alloc ptr 8 align 3
  %value = load i64 %buffer align 32
  return %value
}
})";
        auto bad_alignment = forge::ir::parse_module(invalid_alignment);
        require(bad_alignment.ok(), "invalid-alignment fixture did not parse");
        require(!forge::ir::verify_module(*bad_alignment.module).empty(), "verifier accepted invalid non-power-of-two alignment");

        std::cout << "Forge IR and optimization tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "test failure: " << error.what() << '\n';
        return 1;
    }
}
