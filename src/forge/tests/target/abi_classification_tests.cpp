// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

// Aggregate ABI classification for System V x86-64 and Windows x64. The two
// ABIs disagree about when an aggregate travels in registers, and getting that
// boundary wrong silently corrupts arguments at every native call.

#include <cstdlib>
#include <iostream>
#include <string>

#include "forge/ir/module.hpp"
#include "forge/target/abi.hpp"

namespace {

int failures = 0;

void check(bool condition, const std::string& what) {
    if (condition) return;
    std::cerr << "FAIL " << what << '\n';
    ++failures;
}

forge::ir::StructDecl make_struct(std::string name, std::vector<forge::ir::Type> fields) {
    forge::ir::StructDecl declaration;
    declaration.name = std::move(name);
    for (std::size_t index = 0; index < fields.size(); ++index) {
        declaration.fields.emplace_back("f" + std::to_string(index), fields[index]);
    }
    return declaration;
}

void test_small_aggregates_use_registers() {
    forge::ir::Module module;
    module.structs().push_back(make_struct("Pair", {forge::ir::i32_type(), forge::ir::i32_type()}));

    for (const auto abi : {forge::target::NativeAbi::system_v_x86_64,
                           forge::target::NativeAbi::windows_x64}) {
        const auto classified = forge::target::classify_aggregate(
            module, forge::ir::AggregateRefKind::structure, "Pair", abi);
        check(classified.has_value(), "an 8-byte struct classifies");
        if (!classified) continue;
        check(classified->size == 8, "two i32 fields occupy 8 bytes");
        check(classified->alignment == 4, "alignment follows the widest field");
        check(classified->register_passed(), "an 8-byte struct is register-passed by both ABIs");
        check(!classified->passed_indirectly, "an 8-byte struct is not passed indirectly");
    }
}

void test_large_aggregates_go_indirect() {
    forge::ir::Module module;
    module.structs().push_back(make_struct(
        "Wide", {forge::ir::i64_type(), forge::ir::i64_type(), forge::ir::i64_type(),
                 forge::ir::i64_type()}));

    for (const auto abi : {forge::target::NativeAbi::system_v_x86_64,
                           forge::target::NativeAbi::windows_x64}) {
        const auto classified = forge::target::classify_aggregate(
            module, forge::ir::AggregateRefKind::structure, "Wide", abi);
        check(classified.has_value(), "a 32-byte struct classifies");
        if (!classified) continue;
        check(classified->size == 32, "four i64 fields occupy 32 bytes");
        // Neither ABI passes a 32-byte aggregate in registers: System V exceeds
        // its two-eightbyte window, and Windows x64 only passes powers of two up
        // to 8 bytes.
        check(classified->passed_indirectly, "a 32-byte struct is passed indirectly");
        check(!classified->register_passed(), "an indirect aggregate is not register-passed");
    }
}

void test_windows_rejects_three_byte_aggregates() {
    forge::ir::Module module;
    module.structs().push_back(make_struct(
        "Trio", {forge::ir::i8_type(), forge::ir::i8_type(), forge::ir::i8_type()}));

    // Windows x64 passes an aggregate in a register only when its size is
    // exactly 1, 2, 4, or 8 bytes; System V packs the same three bytes into a
    // single eightbyte.
    const auto windows = forge::target::classify_aggregate(
        module, forge::ir::AggregateRefKind::structure, "Trio",
        forge::target::NativeAbi::windows_x64);
    const auto sysv = forge::target::classify_aggregate(
        module, forge::ir::AggregateRefKind::structure, "Trio",
        forge::target::NativeAbi::system_v_x86_64);

    check(windows.has_value() && sysv.has_value(), "a 3-byte struct classifies under both ABIs");
    if (!windows || !sysv) return;
    check(windows->size == 3 && sysv->size == 3, "three i8 fields occupy 3 bytes");
    check(windows->passed_indirectly, "Windows x64 passes a 3-byte aggregate indirectly");
    check(sysv->register_passed(), "System V passes a 3-byte aggregate in one register");
}

void test_aapcs64_composite_boundaries() {
    forge::ir::Module module;
    module.structs().push_back(make_struct(
        "Trio32", {forge::ir::i32_type(), forge::ir::i32_type(), forge::ir::i32_type()}));
    module.structs().push_back(make_struct(
        "Hfa3", {forge::ir::f32_type(), forge::ir::f32_type(), forge::ir::f32_type()}));

    const auto trio = forge::target::classify_aggregate(
        module, forge::ir::AggregateRefKind::structure, "Trio32",
        forge::target::NativeAbi::aapcs64);
    check(trio.has_value(), "AAPCS64 12-byte composite classifies");
    if (trio) {
        check(trio->size == 12U && trio->register_count == 2U,
              "AAPCS64 12-byte composite uses two machine pieces");
        check(trio->piece_widths.size() == 2U && trio->piece_widths[0] == 8U && trio->piece_widths[1] == 4U,
              "AAPCS64 preserves the 4-byte tail of a 12-byte composite");
    }

    const auto hfa = forge::target::classify_aggregate(
        module, forge::ir::AggregateRefKind::structure, "Hfa3",
        forge::target::NativeAbi::aapcs64);
    check(hfa.has_value() && hfa->homogeneous_float,
          "AAPCS64 recognizes a three-member f32 HFA");
    if (hfa) {
        check(hfa->register_count == 3U && hfa->piece_widths == std::vector<std::uint8_t>({4U, 4U, 4U}),
              "AAPCS64 HFA pieces retain their 32-bit member width");
    }

    forge::ir::Function integer_boundary;
    integer_boundary.name = "integer_boundary";
    for (std::size_t index = 0; index < 7U; ++index)
        integer_boundary.parameters.emplace_back("x" + std::to_string(index), forge::ir::i64_type());
    forge::ir::ValueDecl trio_parameter{"trio", forge::ir::ptr_type()};
    trio_parameter.aggregate_kind = forge::ir::AggregateRefKind::structure;
    trio_parameter.aggregate_name = "Trio32";
    trio_parameter.owned = true;
    integer_boundary.parameters.push_back(trio_parameter);
    integer_boundary.parameters.emplace_back("tail", forge::ir::i64_type());
    const auto integer_abi = forge::target::classify_function(
        module, integer_boundary, forge::target::NativeAbi::aapcs64);
    check(integer_abi.integer_registers == 8U,
          "AAPCS64 exhausts x0-x7 when a composite cannot fit atomically");
    check(integer_abi.stack_bytes == 24U,
          "AAPCS64 stacks the whole 12-byte composite plus the following integer");

    forge::ir::Function floating_boundary;
    floating_boundary.name = "floating_boundary";
    for (std::size_t index = 0; index < 7U; ++index)
        floating_boundary.parameters.emplace_back("v" + std::to_string(index), forge::ir::f32_type());
    forge::ir::ValueDecl hfa_parameter{"hfa", forge::ir::ptr_type()};
    hfa_parameter.aggregate_kind = forge::ir::AggregateRefKind::structure;
    hfa_parameter.aggregate_name = "Hfa3";
    hfa_parameter.owned = true;
    floating_boundary.parameters.push_back(hfa_parameter);
    floating_boundary.parameters.emplace_back("tail", forge::ir::f32_type());
    const auto floating_abi = forge::target::classify_function(
        module, floating_boundary, forge::target::NativeAbi::aapcs64);
    check(floating_abi.floating_registers == 8U,
          "AAPCS64 exhausts v0-v7 when an HFA cannot fit atomically");
    check(floating_abi.stack_bytes == 24U,
          "AAPCS64 stacks the whole HFA plus the following float");
}

void test_darwin_arm64_stack_layout_is_distinct() {
    forge::ir::Module module;
    forge::ir::Function narrow;
    narrow.name = "narrow_stack";
    for (std::size_t index = 0; index < 10U; ++index)
        narrow.parameters.emplace_back("b" + std::to_string(index), forge::ir::i8_type());

    const auto generic = forge::target::classify_function(
        module, narrow, forge::target::NativeAbi::aapcs64);
    const auto darwin = forge::target::classify_function(
        module, narrow, forge::target::NativeAbi::darwin_arm64);
    check(generic.integer_registers == 8U && darwin.integer_registers == 8U,
          "generic and Darwin arm64 both use x0-x7 for the first eight narrow arguments");
    check(generic.stack_bytes == 16U,
          "generic AAPCS64 gives two overflow i8 arguments eight-byte stack slots");
    check(darwin.stack_bytes == 2U,
          "Darwin arm64 tightly packs two fixed overflow i8 arguments at natural width");

    forge::ir::Function mixed;
    mixed.name = "mixed_stack";
    for (std::size_t index = 0; index < 8U; ++index)
        mixed.parameters.emplace_back("x" + std::to_string(index), forge::ir::i64_type());
    mixed.parameters.emplace_back("a", forge::ir::i8_type());
    mixed.parameters.emplace_back("b", forge::ir::i32_type());
    const auto mixed_darwin = forge::target::classify_function(
        module, mixed, forge::target::NativeAbi::darwin_arm64);
    check(mixed_darwin.stack_bytes == 8U,
          "Darwin arm64 respects natural alignment while tightly packing fixed overflow scalars");
}

void test_unknown_aggregate_is_rejected() {
    const forge::ir::Module module;
    const auto classified = forge::target::classify_aggregate(
        module, forge::ir::AggregateRefKind::structure, "Absent",
        forge::target::NativeAbi::system_v_x86_64);
    check(!classified.has_value(), "an undeclared aggregate does not classify");
}

void test_value_class_names_are_stable() {
    using forge::target::abi_value_class_name;
    using forge::target::AbiValueClass;
    check(std::string(abi_value_class_name(AbiValueClass::indirect)) == "indirect", "indirect name");
    check(std::string(abi_value_class_name(AbiValueClass::integer)) == "integer", "integer name");
    check(std::string(abi_value_class_name(AbiValueClass::sse)) == "sse", "sse name");
    check(std::string(abi_value_class_name(AbiValueClass::memory)) == "memory", "memory name");
}

} // namespace

int main() {
    test_small_aggregates_use_registers();
    test_large_aggregates_go_indirect();
    test_windows_rejects_three_byte_aggregates();
    test_aapcs64_composite_boundaries();
    test_darwin_arm64_stack_layout_is_distinct();
    test_unknown_aggregate_is_rejected();
    test_value_class_names_are_stable();
    if (failures != 0) {
        std::cerr << failures << " ABI classification check(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "ABI classification: System V / Windows x64 / AAPCS64 / Darwin arm64 PASS\n";
    return EXIT_SUCCESS;
}
