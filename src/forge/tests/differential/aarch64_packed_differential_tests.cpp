// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

// Executes the packed pseudos that the AArch64 SLP passes form.
//
// The host is x86-64, so AArch64 machine code cannot run here. The packed
// pseudos are target independent, though, so lowering a fixture for AArch64 and
// then handing that machine module to the x86-64 JIT executes exactly the
// shapes the AArch64 recognizers produce. The interpreter runs the original
// scalar IR, so any disagreement means formation changed the program's meaning.

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "forge/interpreter/interpreter.hpp"
#include "forge/ir/parser.hpp"
#include "forge/ir/verifier.hpp"
#include "forge/jit/engine.hpp"
#include "forge/jit/invoke.hpp"
#include "forge/machine/lower.hpp"
#include "forge/machine/module.hpp"
#include "forge/machine/verifier.hpp"

namespace {

int failures = 0;

void check(bool condition, const std::string& what) {
    if (condition) return;
    std::cerr << "FAIL " << what << '\n';
    ++failures;
}

void print_diagnostics(const forge::Diagnostics& diagnostics, const char* label) {
    for (const auto& diagnostic : diagnostics) std::cerr << "  " << label << ": " << diagnostic.message << '\n';
}

#if defined(_WIN32)
constexpr auto host_abi = forge::codegen::x86_64::Abi::windows;
#else
constexpr auto host_abi = forge::codegen::x86_64::Abi::system_v;
#endif

using forge::machine::Opcode;

// --- fixture construction ---------------------------------------------------
//
// Each fixture fills its source globals with known values, zeroes the
// destination, runs one expression per lane, then reads the destination back
// and folds it into the return value with per-lane weights so a lane
// permutation cannot cancel out.

struct Expression {
    // Node list: a negative entry names source `-(value + 1)`; a non-negative
    // entry is an operation over two earlier nodes.
    struct Node {
        std::string operation; // empty for a source
        std::size_t source{};
        std::size_t lhs{};
        std::size_t rhs{};
    };
    std::vector<Node> nodes;

    static Node source(std::size_t index) { return {"", index, 0U, 0U}; }
    static Node op(std::string operation, std::size_t lhs, std::size_t rhs) {
        return {std::move(operation), 0U, lhs, rhs};
    }
};

std::string build_fixture(const std::string& name, std::size_t lanes, std::size_t sources,
                          const Expression& expression, bool opaque_destination) {
    std::string out;
    const auto line = [&](const std::string& text) { out += "    " + text + "\n"; };
    out += "  func @" + name + "(";
    if (opaque_destination) out += "%dst: ptr";
    out += ") -> i32 {\n  entry:\n";
    std::int32_t seed = 12345;
    const auto next = [&]() {
        seed = seed * 1103515245 + 12345;
        return (seed >> 8) % 5000 - 2500;
    };
    for (std::size_t source = 0; source < sources; ++source) {
        const auto base = "%s" + std::to_string(source) + "base";
        line(base + " = global.address ptr @s" + std::to_string(source));
        for (std::size_t lane = 0; lane < lanes; ++lane) {
            const auto tag = std::to_string(source) + "_" + std::to_string(lane);
            line("%fp" + tag + " = ptr.offset ptr " + base + ", " + std::to_string(lane * 4U));
            line("%fv" + tag + " = const i32 " + std::to_string(next()));
            line("store i32 %fv" + tag + ", %fp" + tag + " align 4");
        }
    }
    if (opaque_destination) line("%obase = copy ptr %dst");
    else line("%obase = global.address ptr @dst");
    for (std::size_t lane = 0; lane < lanes; ++lane) {
        line("%zp" + std::to_string(lane) + " = ptr.offset ptr %obase, " + std::to_string(lane * 4U));
        line("%zv" + std::to_string(lane) + " = const i32 0");
        line("store i32 %zv" + std::to_string(lane) + ", %zp" + std::to_string(lane) + " align 4");
    }
    for (std::size_t lane = 0; lane < lanes; ++lane) {
        for (std::size_t index = 0; index < expression.nodes.size(); ++index) {
            const auto& node = expression.nodes[index];
            const auto tag = std::to_string(index) + "_" + std::to_string(lane);
            if (node.operation.empty()) {
                line("%lp" + tag + " = ptr.offset ptr %s" + std::to_string(node.source) + "base, " +
                     std::to_string(lane * 4U));
                line("%n" + tag + " = load i32 %lp" + tag + " align 4");
            } else {
                line("%n" + tag + " = " + node.operation + " i32 %n" + std::to_string(node.lhs) + "_" +
                     std::to_string(lane) + ", %n" + std::to_string(node.rhs) + "_" + std::to_string(lane));
            }
        }
        line("%op" + std::to_string(lane) + " = ptr.offset ptr %obase, " + std::to_string(lane * 4U));
        line("store i32 %n" + std::to_string(expression.nodes.size() - 1U) + "_" + std::to_string(lane) +
             ", %op" + std::to_string(lane) + " align 4");
    }
    for (std::size_t lane = 0; lane < lanes; ++lane) {
        line("%rp" + std::to_string(lane) + " = ptr.offset ptr %obase, " + std::to_string(lane * 4U));
        line("%rv" + std::to_string(lane) + " = load i32 %rp" + std::to_string(lane) + " align 4");
        line("%w" + std::to_string(lane) + " = const i32 " + std::to_string(1U + lane * 31U));
        line("%m" + std::to_string(lane) + " = mul i32 %rv" + std::to_string(lane) + ", %w" +
             std::to_string(lane));
    }
    std::string previous = "%m0";
    for (std::size_t lane = 1; lane < lanes; ++lane) {
        line("%acc" + std::to_string(lane) + " = add i32 " + previous + ", %m" + std::to_string(lane));
        previous = "%acc" + std::to_string(lane);
    }
    line("return " + previous);
    out += "  }\n";
    return out;
}

std::string wrap_module(const std::string& body, std::size_t sources) {
    std::string out = "module @packed_differential {\n";
    for (std::size_t source = 0; source < sources; ++source)
        out += "  global @s" + std::to_string(source) + ": i8[64] align 16 = zero\n";
    out += "  global @dst: i8[64] align 16 = zero\n";
    out += body;
    out += "}\n";
    return out;
}

// --- execution --------------------------------------------------------------

// Lowers for AArch64, checks that the expected pseudo was (or was not) formed,
// then executes the machine module and compares against the scalar interpreter.
void run_case(const std::string& what, const std::string& source, const std::string& entry,
              Opcode expected, bool expect_formed) {
    auto parsed = forge::ir::parse_module(source);
    check(parsed.ok(), what + " parses");
    if (!parsed.ok()) {
        print_diagnostics(parsed.diagnostics, "parse");
        return;
    }
    check(forge::ir::verify_module(*parsed.module).empty(), what + " verifies");

    auto lowered = forge::machine::lower_module(
        *parsed.module, {forge::machine::TargetArchitecture::aarch64});
    check(lowered.ok(), what + " lowers for AArch64");
    if (!lowered.ok()) {
        print_diagnostics(lowered.diagnostics, "lower");
        return;
    }
    const auto machine_verification = forge::machine::verify_module(*lowered.module);
    check(machine_verification.empty(), what + " produces verifiable machine IR");
    if (!machine_verification.empty()) {
        print_diagnostics(machine_verification, "machine");
        return;
    }

    bool formed = false;
    for (const auto& function : lowered.module->functions)
        for (const auto& block : function.blocks)
            for (const auto& instruction : block.instructions)
                if (instruction.opcode == expected) formed = true;
    check(formed == expect_formed,
          what + (expect_formed ? " forms its packed pseudo" : " leaves the run scalar"));

    // Executing the AArch64-lowered module on the x86-64 JIT is what makes this
    // a semantic check rather than a shape check.
    auto loaded = forge::jit::load(*lowered.module, host_abi);
    check(loaded.ok(), what + " loads into the JIT");
    if (!loaded.ok()) {
        print_diagnostics(loaded.diagnostics, "jit");
        return;
    }
    void* address = loaded.engine->lookup(entry);
    check(address != nullptr, what + " exposes its entry point");
    if (address == nullptr) return;

    auto interpreted = forge::interpreter::execute(*parsed.module, entry, {});
    check(interpreted.diagnostics.empty(), what + " interprets");
    if (!interpreted.diagnostics.empty()) {
        print_diagnostics(interpreted.diagnostics, "interpreter");
        return;
    }
    const auto invocation = forge::jit::invoke_integer(address, std::span<const std::uint64_t>{});
    check(invocation.ok(), what + " invokes");
    if (!invocation.ok()) return;

    const auto expected_bits = static_cast<std::uint32_t>(interpreted.value ? interpreted.value->bits() : 0U);
    const auto actual_bits = static_cast<std::uint32_t>(invocation.bits);
    check(expected_bits == actual_bits,
          what + " computes the same result packed as it does scalar (interpreter=" +
              std::to_string(static_cast<std::int32_t>(expected_bits)) + ", packed=" +
              std::to_string(static_cast<std::int32_t>(actual_bits)) + ")");
}

// The broadcast-scalar map: dst[i] = src[i] op k, where k is loop invariant.
// The scalar has to be a runtime value -- a constant is folded into an
// immediate form before this recognizer runs, which leaves the lane
// arithmetic with a single operand and blocks packing entirely.
std::string build_scalar_map_fixture(const std::string& name, std::size_t lanes,
                                     const std::string& destination, bool opaque_destination) {
    std::string out;
    const auto line = [&](const std::string& text) { out += "    " + text + "\n"; };
    out += "  func @" + name + "(" + (opaque_destination ? "%arg: ptr" : "") + ") -> i32 {\n  entry:\n";
    line("%pk = global.address ptr @kval");
    line("%k = load i32 %pk align 4");
    line("%src = global.address ptr @s0");
    for (std::size_t lane = 0; lane < lanes; ++lane) {
        line("%fp" + std::to_string(lane) + " = ptr.offset ptr %src, " + std::to_string(lane * 4U));
        line("%fv" + std::to_string(lane) + " = const i32 " + std::to_string(100U + lane * 7U));
        line("store i32 %fv" + std::to_string(lane) + ", %fp" + std::to_string(lane) + " align 4");
    }
    if (opaque_destination) line("%dstbase = copy ptr %arg");
    else line("%dstbase = global.address ptr @" + destination);
    for (std::size_t lane = 0; lane < lanes; ++lane) {
        // `shift` offsets the destination by one lane inside the source global,
        // which is the overlapping case the proof has to reject.
        const auto shift = destination == "s0" && !opaque_destination ? 4U : 0U;
        line("%sp" + std::to_string(lane) + " = ptr.offset ptr %src, " + std::to_string(lane * 4U + shift));
        line("%dp" + std::to_string(lane) + " = ptr.offset ptr %dstbase, " + std::to_string(lane * 4U));
        line("%v" + std::to_string(lane) + " = load i32 %sp" + std::to_string(lane) + " align 4");
        line("%r" + std::to_string(lane) + " = add i32 %v" + std::to_string(lane) + ", %k");
        line("store i32 %r" + std::to_string(lane) + ", %dp" + std::to_string(lane) + " align 4");
    }
    for (std::size_t lane = 0; lane < lanes; ++lane) {
        line("%rp" + std::to_string(lane) + " = ptr.offset ptr %dstbase, " + std::to_string(lane * 4U));
        line("%rv" + std::to_string(lane) + " = load i32 %rp" + std::to_string(lane) + " align 4");
        line("%w" + std::to_string(lane) + " = const i32 " + std::to_string(1U + lane * 31U));
        line("%m" + std::to_string(lane) + " = mul i32 %rv" + std::to_string(lane) + ", %w" +
             std::to_string(lane));
    }
    std::string previous = "%m0";
    for (std::size_t lane = 1; lane < lanes; ++lane) {
        line("%acc" + std::to_string(lane) + " = add i32 " + previous + ", %m" + std::to_string(lane));
        previous = "%acc" + std::to_string(lane);
    }
    line("return " + previous);
    out += "  }\n";
    return out;
}

std::string wrap_scalar_map_module(const std::string& body) {
    return "module @scalar_map {\n"
           "  global @s0: i8[64] align 16 = zero\n"
           "  global @dst: i8[64] align 16 = zero\n"
           "  global @kval: i8[4] align 4 = zero\n" + body + "}\n";
}

void test_separate_destination_map() {
    // Source and destination are distinct globals, so provenance proves the
    // runs disjoint and the separate-destination map is selected.
    run_case("a scalar map into a separate global",
             wrap_scalar_map_module(build_scalar_map_fixture("map", 4, "dst", false)),
             "map", Opcode::binary_i32_contiguous_map, true);
}

void test_in_place_map_still_selected() {
    // Same base, same offsets: still the in-place form, which needs no proof.
    std::string out;
    const auto line = [&](const std::string& text) { out += "    " + text + "\n"; };
    out += "  func @inplace() -> i32 {\n  entry:\n";
    line("%pk = global.address ptr @kval");
    line("%k = load i32 %pk align 4");
    line("%src = global.address ptr @s0");
    for (std::size_t lane = 0; lane < 4U; ++lane) {
        line("%fp" + std::to_string(lane) + " = ptr.offset ptr %src, " + std::to_string(lane * 4U));
        line("%fv" + std::to_string(lane) + " = const i32 " + std::to_string(100U + lane * 7U));
        line("store i32 %fv" + std::to_string(lane) + ", %fp" + std::to_string(lane) + " align 4");
    }
    for (std::size_t lane = 0; lane < 4U; ++lane) {
        line("%sp" + std::to_string(lane) + " = ptr.offset ptr %src, " + std::to_string(lane * 4U));
        line("%v" + std::to_string(lane) + " = load i32 %sp" + std::to_string(lane) + " align 4");
        line("%r" + std::to_string(lane) + " = add i32 %v" + std::to_string(lane) + ", %k");
        line("store i32 %r" + std::to_string(lane) + ", %sp" + std::to_string(lane) + " align 4");
    }
    for (std::size_t lane = 0; lane < 4U; ++lane) {
        line("%rp" + std::to_string(lane) + " = ptr.offset ptr %src, " + std::to_string(lane * 4U));
        line("%rv" + std::to_string(lane) + " = load i32 %rp" + std::to_string(lane) + " align 4");
        line("%w" + std::to_string(lane) + " = const i32 " + std::to_string(1U + lane * 31U));
        line("%m" + std::to_string(lane) + " = mul i32 %rv" + std::to_string(lane) + ", %w" +
             std::to_string(lane));
    }
    line("%acc1 = add i32 %m0, %m1");
    line("%acc2 = add i32 %acc1, %m2");
    line("%acc3 = add i32 %acc2, %m3");
    line("return %acc3");
    out += "  }\n";
    run_case("an in-place scalar map", wrap_scalar_map_module(out), "inplace",
             Opcode::binary_i32_contiguous_inplace, true);
}

void test_shifted_scalar_map_is_not_packed() {
    // dst[i] = src[i + 1] op k. The scalar-map recognizer only groups a run
    // whose first lane sits exactly at its resolved base, so a shifted source
    // never reaches the aliasing proof -- but it must still stay scalar, since
    // here the shifted source and the destination are the same global and do
    // overlap. (The multi-source recognizer, whose lanes may start at a
    // non-zero offset, is where the proof's range test does the work; see the
    // opaque-destination cases.)
    run_case("a scalar map reading one lane ahead of its destination",
             wrap_scalar_map_module(build_scalar_map_fixture("overlap", 4, "s0", false)),
             "overlap", Opcode::binary_i32_contiguous_map, false);
}

void test_opaque_map_destination_is_not_packed() {
    auto source = wrap_scalar_map_module(build_scalar_map_fixture("omap", 4, "dst", true));
    source.pop_back();
    source.pop_back();
    source +=
        "  func @omap_entry() -> i32 {\n"
        "  entry:\n"
        "    %d = global.address ptr @dst\n"
        "    %r = call i32 @omap(%d)\n"
        "    return %r\n"
        "  }\n}\n";
    run_case("a scalar map into an opaque pointer", source, "omap_entry",
             Opcode::binary_i32_contiguous_map, false);
}

void test_two_source_map() {
    // dst[i] = s0[i] op s1[i]: a single operation over two source runs, which
    // is the most specific pseudo in the family.
    Expression expression;
    expression.nodes = {Expression::source(0), Expression::source(1), Expression::op("add", 0, 1)};
    run_case("a single-operation lane run", wrap_module(build_fixture("map2", 4, 2, expression, false), 2),
             "map2", Opcode::binary_i32_contiguous_map2, true);
}

void test_three_source_map() {
    // dst[i] = (s0[i] ^ s1[i]) - s2[i]: two operations, left-leaning.
    Expression expression;
    expression.nodes = {Expression::source(0), Expression::source(1), Expression::op("xor", 0, 1),
                        Expression::source(2), Expression::op("sub", 2, 3)};
    run_case("a two-operation left-leaning run",
             wrap_module(build_fixture("map3", 4, 3, expression, false), 3),
             "map3", Opcode::binary_i32_contiguous_map3, true);
}

void test_right_leaning_pair_stays_a_dag() {
    // dst[i] = s0[i] - (s1[i] ^ s2[i]). The three-source map only expresses
    // ((a op b) op c), so a right-leaning pair must fall through to the DAG
    // form rather than being mis-selected into map3.
    Expression expression;
    expression.nodes = {Expression::source(0), Expression::source(1), Expression::source(2),
                        Expression::op("xor", 1, 2), Expression::op("sub", 0, 3)};
    auto source = wrap_module(build_fixture("rightlean", 4, 3, expression, false), 3);
    run_case("a two-operation right-leaning run", source, "rightlean",
             Opcode::binary_i32_contiguous_dag, true);
    run_case("a two-operation right-leaning run (not a three-source map)", source, "rightlean",
             Opcode::binary_i32_contiguous_map3, false);
}

void test_chain() {
    // ((s0 + s1) ^ s2) - s3: a left-leaning chain of four source runs.
    Expression expression;
    expression.nodes = {Expression::source(0), Expression::source(1), Expression::op("add", 0, 1),
                        Expression::source(2), Expression::op("xor", 2, 3),
                        Expression::source(3), Expression::op("sub", 4, 5)};
    run_case("a four-source chain", wrap_module(build_fixture("chain", 4, 4, expression, false), 4),
             "chain", Opcode::binary_i32_contiguous_chain, true);
}

void test_dag() {
    // (s0 + s1) - (s2 ^ s3): a balanced tree, so a postfix DAG rather than a chain.
    Expression expression;
    expression.nodes = {Expression::source(0), Expression::source(1), Expression::op("add", 0, 1),
                        Expression::source(2), Expression::source(3), Expression::op("xor", 3, 4),
                        Expression::op("sub", 2, 5)};
    run_case("a balanced expression tree", wrap_module(build_fixture("dag", 4, 4, expression, false), 4),
             "dag", Opcode::binary_i32_contiguous_dag, true);
}

void test_reusable_dag() {
    // t = s0 + s1 consumed twice, which only the reusable form expresses
    // without recomputing the subtree.
    Expression expression;
    expression.nodes = {Expression::source(0), Expression::source(1), Expression::op("add", 0, 1),
                        Expression::source(2), Expression::op("xor", 2, 3), Expression::op("sub", 4, 2)};
    run_case("an expression with a shared subterm",
             wrap_module(build_fixture("reuse", 4, 3, expression, false), 3),
             "reuse", Opcode::binary_i32_contiguous_dag_reuse, true);
}

void test_eight_lane_chain() {
    Expression expression;
    expression.nodes = {Expression::source(0), Expression::source(1), Expression::op("and", 0, 1),
                        Expression::source(2), Expression::op("or", 2, 3),
                        Expression::source(3), Expression::op("add", 4, 5)};
    run_case("an eight-lane chain", wrap_module(build_fixture("chain8", 8, 4, expression, false), 4),
             "chain8", Opcode::binary_i32_contiguous_chain, true);
}

void test_opaque_destination_is_not_packed() {
    // Nothing proves an incoming pointer disjoint from the source globals, so
    // the lane loads may not be hoisted above the lane stores.
    Expression expression;
    expression.nodes = {Expression::source(0), Expression::source(1), Expression::op("add", 0, 1),
                        Expression::source(2), Expression::op("xor", 2, 3),
                        Expression::source(3), Expression::op("sub", 4, 5)};
    auto source = wrap_module(build_fixture("opaque", 4, 4, expression, true), 4);
    // The fixture takes the destination as a parameter, so drive it from a
    // wrapper that hands it a global and returns the same checksum.
    source.pop_back();
    source.pop_back();
    source +=
        "  func @opaque_entry() -> i32 {\n"
        "  entry:\n"
        "    %d = global.address ptr @dst\n"
        "    %r = call i32 @opaque(%d)\n"
        "    return %r\n"
        "  }\n}\n";
    run_case("a run whose destination is an opaque pointer", source, "opaque_entry",
             Opcode::binary_i32_contiguous_chain, false);
}

// A packed pseudo writes memory and defines no value. If a pass mistakes it for
// a value-producing instruction whose result is unused, the whole run is
// deleted and the stores silently vanish.
void test_packed_store_is_not_dead_code() {
    const std::string source = R"(
module @packed_liveness {
  global @data: i8[64] align 16 = zero
  func @scale(%unused: i32, %s: i32) -> i32 {
  entry:
    %p = global.address ptr @data
    %p0 = ptr.offset ptr %p, 0
    %p1 = ptr.offset ptr %p, 4
    %p2 = ptr.offset ptr %p, 8
    %p3 = ptr.offset ptr %p, 12
    %i0 = const i32 10
    %i1 = const i32 20
    %i2 = const i32 30
    %i3 = const i32 40
    store i32 %i0, %p0 align 4
    store i32 %i1, %p1 align 4
    store i32 %i2, %p2 align 4
    store i32 %i3, %p3 align 4
    %a = load i32 %p0 align 4
    %b = load i32 %p1 align 4
    %c = load i32 %p2 align 4
    %d = load i32 %p3 align 4
    %a2 = add i32 %a, %s
    %b2 = add i32 %b, %s
    %c2 = add i32 %c, %s
    %d2 = add i32 %d, %s
    store i32 %a2, %p0 align 4
    store i32 %b2, %p1 align 4
    store i32 %c2, %p2 align 4
    store i32 %d2, %p3 align 4
    %r0 = load i32 %p0 align 4
    %r3 = load i32 %p3 align 4
    %sum = add i32 %r0, %r3
    return %sum
  }
}
)";
    auto parsed = forge::ir::parse_module(source);
    check(parsed.ok(), "the packed-liveness fixture parses");
    if (!parsed.ok()) return;
    auto lowered = forge::machine::lower_module(
        *parsed.module, {forge::machine::TargetArchitecture::aarch64});
    check(lowered.ok(), "the packed-liveness fixture lowers");
    if (!lowered.ok()) return;

    bool packed = false;
    for (const auto& function : lowered.module->functions)
        for (const auto& block : function.blocks)
            for (const auto& instruction : block.instructions)
                if (instruction.opcode == Opcode::binary_i32_contiguous_inplace) packed = true;
    // The first virtual register belongs to the unused parameter, which is
    // exactly the number a packed pseudo's unset result field would claim.
    check(packed, "the in-place map survives when virtual register zero is unused");
}

} // namespace

int main() {
    test_separate_destination_map();
    test_in_place_map_still_selected();
    test_shifted_scalar_map_is_not_packed();
    test_opaque_map_destination_is_not_packed();
    test_two_source_map();
    test_three_source_map();
    test_right_leaning_pair_stays_a_dag();
    test_chain();
    test_dag();
    test_reusable_dag();
    test_eight_lane_chain();
    test_opaque_destination_is_not_packed();
    test_packed_store_is_not_dead_code();
    if (failures != 0) {
        std::cerr << failures << " AArch64 packed differential checks failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "AArch64 packed differential tests passed\n";
    return EXIT_SUCCESS;
}
