// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

// Data layout: scalar sizes/alignments, alignment arithmetic, and aggregate
// layout resolved through a module so named element types are followed.

#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

#include "forge/ir/module.hpp"
#include "forge/platform/data_layout.hpp"

namespace {

int failures = 0;

void check(bool condition, const std::string& what) {
    if (condition) return;
    std::cerr << "FAIL " << what << '\n';
    ++failures;
}

template <typename T>
void check_eq(const T& actual, const T& expected, const std::string& what) {
    if (actual == expected) return;
    std::cerr << "FAIL " << what << ": expected " << expected << ", got " << actual << '\n';
    ++failures;
}

void test_alignment_arithmetic() {
    using namespace forge::target;

    check(is_power_of_two(1), "1 is a power of two");
    check(is_power_of_two(64), "64 is a power of two");
    check(!is_power_of_two(0), "zero is not a power of two");
    check(!is_power_of_two(24), "24 is not a power of two");

    check(is_aligned(0, 8), "zero is aligned to any power of two");
    check(is_aligned(16, 8), "16 is 8-aligned");
    check(!is_aligned(12, 8), "12 is not 8-aligned");
    check(!is_aligned(8, 3), "a non-power-of-two alignment is never satisfied");

    check_eq(align_to(0, 8), std::size_t{0}, "align_to leaves zero alone");
    check_eq(align_to(1, 8), std::size_t{8}, "align_to rounds up");
    check_eq(align_to(8, 8), std::size_t{8}, "align_to is idempotent on aligned values");

    // Overflow must be reported, not wrapped: a wrapped offset would silently
    // place a field before the start of its aggregate.
    const auto huge = std::numeric_limits<std::size_t>::max() - 3;
    check(!checked_align_to(huge, 64).has_value(), "checked_align_to reports overflow");
    check(!checked_align_to(8, 24).has_value(), "checked_align_to rejects non-power-of-two");
    check(checked_align_to(1, 8).value_or(0) == 8, "checked_align_to rounds up");
}

void test_scalar_layout() {
    const auto layout = forge::target::DataLayout::host();
    check(forge::target::is_power_of_two(layout.pointer_alignment),
          "the host pointer alignment is valid");
    check(layout.pointer_size != 0, "the host pointer size is non-zero");

    check_eq(layout.size_of(forge::ir::i8_type()).value_or(0), std::size_t{1}, "i8 size");
    check_eq(layout.size_of(forge::ir::i16_type()).value_or(0), std::size_t{2}, "i16 size");
    check_eq(layout.size_of(forge::ir::i32_type()).value_or(0), std::size_t{4}, "i32 size");
    check_eq(layout.size_of(forge::ir::i64_type()).value_or(0), std::size_t{8}, "i64 size");
    check_eq(layout.size_of(forge::ir::f32_type()).value_or(0), std::size_t{4}, "f32 size");
    check_eq(layout.size_of(forge::ir::f64_type()).value_or(0), std::size_t{8}, "f64 size");

    check_eq(layout.alignment_of(forge::ir::i32_type()).value_or(0), std::size_t{4}, "i32 alignment");
    check_eq(layout.alignment_of(forge::ir::i64_type()).value_or(0), std::size_t{8}, "i64 alignment");

    check_eq(layout.size_of(forge::ir::ptr_type()).value_or(0), layout.pointer_size, "pointer size");
    check_eq(layout.alignment_of(forge::ir::ptr_type()).value_or(0), layout.pointer_alignment,
             "pointer alignment");

    // Every scalar size the layout reports must be a valid alignment too, or
    // aggregate placement below cannot be trusted.
    for (const auto type : {forge::ir::i8_type(), forge::ir::i16_type(), forge::ir::i32_type(),
                            forge::ir::i64_type(), forge::ir::f32_type(), forge::ir::f64_type()}) {
        const auto alignment = layout.alignment_of(type).value_or(0);
        check(forge::target::is_power_of_two(alignment), "scalar alignment is a power of two");
    }
}

void test_struct_and_array_layout() {
    forge::ir::Module module;

    forge::ir::StructDecl mixed;
    mixed.name = "Mixed";
    mixed.fields.emplace_back("a", forge::ir::i8_type());
    mixed.fields.emplace_back("b", forge::ir::i64_type());
    mixed.fields.emplace_back("c", forge::ir::i16_type());
    module.structs().push_back(mixed);

    const auto layout = forge::target::DataLayout::host();
    const auto resolved = layout.struct_layout(module, module.structs().front());
    check(resolved.has_value(), "a mixed struct lays out");
    if (resolved) {
        check_eq(resolved->fields.size(), std::size_t{3}, "field count");
        check_eq(resolved->fields[0].offset, std::size_t{0}, "first field starts at zero");
        // i64 must be 8-aligned, so the i8 is followed by seven bytes of padding.
        check_eq(resolved->fields[1].offset, std::size_t{8}, "i64 field is aligned");
        check_eq(resolved->alignment, std::size_t{8}, "struct alignment is the widest member");
        check(forge::target::is_aligned(resolved->size, resolved->alignment),
              "struct size is a multiple of its alignment");
        check(resolved->size >= resolved->fields[2].offset + 2, "struct size covers its last field");
    }

    forge::ir::ArrayDecl array;
    array.name = "Quad";
    array.element_type = forge::ir::i32_type();
    array.element_count = 4;
    module.arrays().push_back(array);

    const auto array_layout = layout.array_layout(module, module.arrays().front());
    check(array_layout.has_value(), "an i32 array lays out");
    if (array_layout) {
        check_eq(array_layout->stride, std::size_t{4}, "i32 array stride");
        check_eq(array_layout->size, std::size_t{16}, "four i32 elements");
        check_eq(array_layout->alignment, std::size_t{4}, "i32 array alignment");
    }
}

} // namespace

int main() {
    test_alignment_arithmetic();
    test_scalar_layout();
    test_struct_and_array_layout();
    if (failures != 0) {
        std::cerr << failures << " data layout check(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "data layout: scalar sizes, alignment arithmetic, struct/array layout PASS\n";
    return EXIT_SUCCESS;
}
