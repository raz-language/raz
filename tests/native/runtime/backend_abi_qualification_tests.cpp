// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "forge/ir/module.hpp"
#include "forge/ir/type.hpp"
#include "forge/machine/module.hpp"
#include "forge/object/archive.hpp"
#include "forge/object/coff.hpp"
#include "forge/object/elf.hpp"
#include "forge/target/abi.hpp"
#include "forge/target/data_layout.hpp"

extern "C" {
std::int64_t raz_rt_abi_pointer_size();
std::int64_t raz_rt_abi_pointer_alignment();
std::int64_t raz_rt_abi_bool_size();
std::int64_t raz_rt_abi_bool_alignment();
std::int64_t raz_rt_abi_i8_size();
std::int64_t raz_rt_abi_i8_alignment();
std::int64_t raz_rt_abi_i16_size();
std::int64_t raz_rt_abi_i16_alignment();
std::int64_t raz_rt_abi_i32_size();
std::int64_t raz_rt_abi_i32_alignment();
std::int64_t raz_rt_abi_i64_size();
std::int64_t raz_rt_abi_i64_alignment();
std::int64_t raz_rt_abi_f32_size();
std::int64_t raz_rt_abi_f32_alignment();
std::int64_t raz_rt_abi_f64_size();
std::int64_t raz_rt_abi_f64_alignment();
std::int64_t raz_rt_abi_size_t_size();
std::int64_t raz_rt_abi_size_t_alignment();
std::int64_t raz_rt_abi_little_endian();
}

namespace {

void expect(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "backend ABI qualification failed: " << message << '\n';
    std::exit(1);
}

forge::ir::StructDecl make_struct(std::string name, std::initializer_list<forge::ir::StructField> fields) {
    forge::ir::StructDecl declaration;
    declaration.name = std::move(name);
    declaration.fields.assign(fields.begin(), fields.end());
    return declaration;
}

} // namespace

int main() {
    using forge::ir::AggregateRefKind;
    using forge::ir::StructField;
    using forge::ir::Type;
    using forge::target::AbiValueClass;
    using forge::target::NativeAbi;

    const auto layout = forge::target::DataLayout::host();
    expect(layout.is_valid(), "Forge host data layout is invalid");
    expect(layout.pointer_size == static_cast<std::size_t>(raz_rt_abi_pointer_size()), "pointer size disagreement");
    expect(layout.pointer_alignment == static_cast<std::size_t>(raz_rt_abi_pointer_alignment()), "pointer alignment disagreement");
    expect((layout.endianness == forge::target::Endianness::little) == (raz_rt_abi_little_endian() != 0), "endianness disagreement");

    const auto check_scalar = [&](Type type, std::int64_t size, std::int64_t alignment, const char* label) {
        expect(layout.size_of(type).value_or(0) == static_cast<std::size_t>(size), label);
        expect(layout.alignment_of(type).value_or(0) == static_cast<std::size_t>(alignment), label);
    };
    check_scalar(forge::ir::i8_type(), raz_rt_abi_i8_size(), raz_rt_abi_i8_alignment(), "i8 layout disagreement");
    check_scalar(forge::ir::i16_type(), raz_rt_abi_i16_size(), raz_rt_abi_i16_alignment(), "i16 layout disagreement");
    check_scalar(forge::ir::i32_type(), raz_rt_abi_i32_size(), raz_rt_abi_i32_alignment(), "i32 layout disagreement");
    check_scalar(forge::ir::i64_type(), raz_rt_abi_i64_size(), raz_rt_abi_i64_alignment(), "i64 layout disagreement");
    check_scalar(forge::ir::f32_type(), raz_rt_abi_f32_size(), raz_rt_abi_f32_alignment(), "f32 layout disagreement");
    check_scalar(forge::ir::f64_type(), raz_rt_abi_f64_size(), raz_rt_abi_f64_alignment(), "f64 layout disagreement");
    expect(raz_rt_abi_bool_size() == static_cast<std::int64_t>(sizeof(bool)), "runtime bool size disagreement");
    expect(raz_rt_abi_bool_alignment() == static_cast<std::int64_t>(alignof(bool)), "runtime bool alignment disagreement");
    expect(raz_rt_abi_size_t_size() == static_cast<std::int64_t>(sizeof(std::size_t)), "runtime size_t size disagreement");
    expect(raz_rt_abi_size_t_alignment() == static_cast<std::int64_t>(alignof(std::size_t)), "runtime size_t alignment disagreement");

    forge::ir::Module module("raz-backend-abi-qualification");
    module.structs().push_back(make_struct("OneWord", {StructField{"value", forge::ir::i64_type()}}));
    module.structs().push_back(make_struct("TwoIntegers", {StructField{"left", forge::ir::i64_type()}, StructField{"right", forge::ir::i64_type()}}));
    module.structs().push_back(make_struct("TwoFloats", {StructField{"left", forge::ir::f64_type()}, StructField{"right", forge::ir::f64_type()}}));
    module.structs().push_back(make_struct("Mixed", {StructField{"number", forge::ir::i64_type()}, StructField{"ratio", forge::ir::f64_type()}}));
    module.structs().push_back(make_struct("Large", {StructField{"a", forge::ir::i64_type()}, StructField{"b", forge::ir::i64_type()}, StructField{"c", forge::ir::i64_type()}}));

    const auto classify = [&](const char* name, NativeAbi abi) {
        return forge::target::classify_aggregate(module, AggregateRefKind::structure, name, abi, layout);
    };

    const auto sysv_one = classify("OneWord", NativeAbi::system_v_x86_64);
    expect(sysv_one && sysv_one->register_passed() && sysv_one->register_count == 1 && sysv_one->classes[0] == AbiValueClass::integer,
           "System V one-word aggregate classification");
    const auto win_one = classify("OneWord", NativeAbi::windows_x64);
    expect(win_one && win_one->register_passed() && win_one->register_count == 1,
           "Windows one-word aggregate classification");

    const auto sysv_two = classify("TwoIntegers", NativeAbi::system_v_x86_64);
    expect(sysv_two && sysv_two->register_passed() && sysv_two->register_count == 2 &&
               sysv_two->classes[0] == AbiValueClass::integer && sysv_two->classes[1] == AbiValueClass::integer,
           "System V two-integer aggregate classification");
    const auto win_two = classify("TwoIntegers", NativeAbi::windows_x64);
    expect(win_two && win_two->passed_indirectly && win_two->returned_indirectly,
           "Windows 16-byte aggregate must be indirect");

    const auto sysv_float = classify("TwoFloats", NativeAbi::system_v_x86_64);
    expect(sysv_float && sysv_float->register_passed() && sysv_float->register_count == 2 &&
               sysv_float->classes[0] == AbiValueClass::sse && sysv_float->classes[1] == AbiValueClass::sse,
           "System V floating aggregate classification");
    const auto sysv_mixed = classify("Mixed", NativeAbi::system_v_x86_64);
    expect(sysv_mixed && sysv_mixed->register_passed() && sysv_mixed->register_count == 2 &&
               sysv_mixed->classes[0] == AbiValueClass::integer && sysv_mixed->classes[1] == AbiValueClass::sse,
           "System V mixed aggregate classification");

    const auto sysv_large = classify("Large", NativeAbi::system_v_x86_64);
    const auto win_large = classify("Large", NativeAbi::windows_x64);
    expect(sysv_large && sysv_large->passed_indirectly && sysv_large->returned_indirectly,
           "System V large aggregate must be indirect");
    expect(win_large && win_large->passed_indirectly && win_large->returned_indirectly,
           "Windows large aggregate must be indirect");

    forge::ir::Function function;
    function.name = "qualified_call";
    function.return_type = forge::ir::void_type();
    function.parameters = {
        forge::ir::ValueDecl{"a", forge::ir::i64_type()}, forge::ir::ValueDecl{"b", forge::ir::i64_type()},
        forge::ir::ValueDecl{"c", forge::ir::i64_type()}, forge::ir::ValueDecl{"d", forge::ir::i64_type()},
        forge::ir::ValueDecl{"e", forge::ir::i64_type()}, forge::ir::ValueDecl{"f", forge::ir::i64_type()},
        forge::ir::ValueDecl{"g", forge::ir::i64_type()}};
    const auto sysv_function = forge::target::classify_function(module, function, NativeAbi::system_v_x86_64, layout);
    const auto win_function = forge::target::classify_function(module, function, NativeAbi::windows_x64, layout);
    expect(sysv_function.integer_registers == 6 && sysv_function.stack_bytes == 8,
           "System V integer register/stack assignment");
    expect(win_function.integer_registers == 4 && win_function.stack_bytes == 24,
           "Windows x64 integer register/stack assignment");

    forge::machine::Module native_module;
    native_module.name = "raz-native-object-qualification";
    forge::machine::Function native_function;
    native_function.name = "raz_qualified_answer";
    native_function.register_count = 1;
    native_function.register_widths = {8};
    native_function.register_classes = {forge::machine::RegisterClass::integer};
    forge::machine::Block native_block;
    native_block.name = "entry";
    native_block.instructions.push_back({forge::machine::Opcode::load_immediate_i64, 0, {}, 42, 0, {}, {}});
    native_block.instructions.push_back({forge::machine::Opcode::return_i64, 0, {0}, 0, 0, {}, {}});
    native_function.blocks.push_back(std::move(native_block));
    native_module.functions.push_back(std::move(native_function));
    const auto elf = forge::object::emit_elf64_x86_64(native_module, forge::codegen::x86_64::Abi::system_v);
    const auto coff = forge::object::emit_coff_x86_64(native_module, forge::codegen::x86_64::Abi::windows);
    expect(elf.ok() && elf.bytes.size() > 64, "System V ELF object emission");
    expect(coff.ok() && coff.bytes.size() > 64, "Windows x64 COFF object emission");
    const std::vector<forge::object::ArchiveMember> elf_members{{"qualified.o", elf.bytes}};
    const std::vector<forge::object::ArchiveMember> coff_members{{"qualified.obj", coff.bytes}};
    const auto elf_archive = forge::object::emit_static_archive(elf_members);
    const auto coff_archive = forge::object::emit_static_archive(coff_members);
    expect(elf_archive.ok() && coff_archive.ok(), "cross-format static archive emission");

    std::cout << "Raz backend ABI qualification passed\n";
    return 0;
}
