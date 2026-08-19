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
    check(std::string(abi_value_class_name(AbiValueClass::none)) == "none", "none name");
    check(std::string(abi_value_class_name(AbiValueClass::integer)) == "integer", "integer name");
    check(std::string(abi_value_class_name(AbiValueClass::sse)) == "sse", "sse name");
    check(std::string(abi_value_class_name(AbiValueClass::memory)) == "memory", "memory name");
}

} // namespace

int main() {
    test_small_aggregates_use_registers();
    test_large_aggregates_go_indirect();
    test_windows_rejects_three_byte_aggregates();
    test_unknown_aggregate_is_rejected();
    test_value_class_names_are_stable();
    if (failures != 0) {
        std::cerr << failures << " ABI classification check(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "ABI classification: System V / Windows x64 aggregate passing PASS\n";
    return EXIT_SUCCESS;
}
