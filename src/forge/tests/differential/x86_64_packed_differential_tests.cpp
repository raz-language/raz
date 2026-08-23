// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

// Executes the packed pseudos that the x86-64 SLP recognizers form.
//
// These pseudos were encoded but never run: the recognizers only fire on a
// narrow instruction layout that no example or benchmark in the tree produces,
// so the encoder paths behind them went unexercised and accumulated defects
// that only show up at run time (a missing SIB byte, a missing REX bit, a
// packed store deleted as dead code). Every fixture here therefore computes a
// value, reads it back out of memory, and compares against the interpreter
// running the same scalar IR.
//
// The fixtures use the layout the x86-64 recognizers require -- all lane loads,
// then all lane arithmetic contiguously, then all lane stores contiguously.

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string>
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

struct LaneShape {
    // Each stage names one contiguous run of lane operations. `lhs`/`rhs` are
    // either "l<n>" for source n's lane load or "t<stage>" for an earlier
    // stage's result.
    struct Stage {
        std::string operation;
        std::string lhs;
        std::string rhs;
    };
    std::vector<Stage> stages;
    std::size_t sources{};
};

std::string build(const std::string& name, const LaneShape& shape, std::size_t lanes,
                  const std::string& type, std::size_t width) {
    std::string out;
    const auto line = [&](const std::string& text) { out += "    " + text + "\n"; };
    const auto lane_suffix = [](const std::string& value, std::size_t lane) {
        return "%" + value + "_" + std::to_string(lane);
    };
    out += "  func @" + name + "() -> " + type + " {\n  entry:\n";

    std::int64_t seed = 20260820;
    const auto next = [&]() {
        seed = seed * 6364136223846793005LL + 1442695040888963407LL;
        return static_cast<std::int32_t>((seed >> 33) % 20000 - 10000);
    };
    for (std::size_t source = 0; source < shape.sources; ++source) {
        line("%b" + std::to_string(source) + " = global.address ptr @s" + std::to_string(source));
        for (std::size_t lane = 0; lane < lanes; ++lane) {
            const auto tag = std::to_string(source) + "_" + std::to_string(lane);
            line("%fp" + tag + " = ptr.offset ptr %b" + std::to_string(source) + ", " +
                 std::to_string(lane * width));
            line("%fv" + tag + " = const " + type + " " + std::to_string(next()));
            line("store " + type + " %fv" + tag + ", %fp" + tag + " align " + std::to_string(width));
        }
    }
    line("%bd = global.address ptr @dst");
    for (std::size_t lane = 0; lane < lanes; ++lane) {
        line("%zp" + std::to_string(lane) + " = ptr.offset ptr %bd, " + std::to_string(lane * width));
        line("%zv" + std::to_string(lane) + " = const " + type + " 0");
        line("store " + type + " %zv" + std::to_string(lane) + ", %zp" + std::to_string(lane) +
             " align " + std::to_string(width));
    }

    // All lane loads.
    for (std::size_t source = 0; source < shape.sources; ++source) {
        for (std::size_t lane = 0; lane < lanes; ++lane) {
            const auto tag = std::to_string(source) + "_" + std::to_string(lane);
            line("%lp" + tag + " = ptr.offset ptr %b" + std::to_string(source) + ", " +
                 std::to_string(lane * width));
            line("%l" + tag + " = load " + type + " %lp" + tag + " align " + std::to_string(width));
        }
    }
    // Each arithmetic stage as one contiguous run, which is what the x86-64
    // recognizers scan for.
    for (std::size_t stage = 0; stage < shape.stages.size(); ++stage) {
        for (std::size_t lane = 0; lane < lanes; ++lane) {
            const auto& entry = shape.stages[stage];
            line(lane_suffix("t" + std::to_string(stage), lane) + " = " + entry.operation + " " + type +
                 " " + lane_suffix(entry.lhs, lane) + ", " + lane_suffix(entry.rhs, lane));
        }
    }
    // All lane stores.
    const auto root = "t" + std::to_string(shape.stages.size() - 1U);
    for (std::size_t lane = 0; lane < lanes; ++lane) {
        line("%sp" + std::to_string(lane) + " = ptr.offset ptr %bd, " + std::to_string(lane * width));
        line("store " + type + " " + lane_suffix(root, lane) + ", %sp" + std::to_string(lane) +
             " align " + std::to_string(width));
    }

    // Read the results back and weight them, so a lane permutation cannot
    // cancel out in the checksum.
    for (std::size_t lane = 0; lane < lanes; ++lane) {
        line("%rp" + std::to_string(lane) + " = ptr.offset ptr %bd, " + std::to_string(lane * width));
        line("%rv" + std::to_string(lane) + " = load " + type + " %rp" + std::to_string(lane) +
             " align " + std::to_string(width));
        line("%w" + std::to_string(lane) + " = const " + type + " " + std::to_string(1U + lane * 31U));
        line("%m" + std::to_string(lane) + " = mul " + type + " %rv" + std::to_string(lane) + ", %w" +
             std::to_string(lane));
    }
    std::string previous = "%m0";
    for (std::size_t lane = 1; lane < lanes; ++lane) {
        line("%acc" + std::to_string(lane) + " = add " + type + " " + previous + ", %m" +
             std::to_string(lane));
        previous = "%acc" + std::to_string(lane);
    }
    line("return " + previous);
    out += "  }\n";
    return out;
}

std::string wrap(const std::string& body, std::size_t sources) {
    std::string out = "module @x86_packed_differential {\n";
    for (std::size_t source = 0; source < sources; ++source)
        out += "  global @s" + std::to_string(source) + ": i8[128] align 16 = zero\n";
    out += "  global @dst: i8[128] align 16 = zero\n";
    return out + body + "}\n";
}

// --- execution --------------------------------------------------------------

void run_case(const std::string& what, const std::string& source, const std::string& entry,
              Opcode expected) {
    auto parsed = forge::ir::parse_module(source);
    check(parsed.ok(), what + " parses");
    if (!parsed.ok()) {
        print_diagnostics(parsed.diagnostics, "parse");
        return;
    }
    check(forge::ir::verify_module(*parsed.module).empty(), what + " verifies");

    // Default lowering targets the host, which is where the x86-64 SLP
    // recognizers run.
    auto lowered = forge::machine::lower_module(*parsed.module);
    check(lowered.ok(), what + " lowers");
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
    // A fixture that stops being recognized silently stops testing the encoder
    // path it exists for, so this is an assertion rather than a skip.
    check(formed, what + " is still recognized by the x86-64 SLP pass");

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

    const auto expected_bits = interpreted.value ? interpreted.value->bits() : 0U;
    const auto actual_bits = invocation.bits;
    check(expected_bits == actual_bits,
          what + " computes the same result packed as it does scalar (interpreter=" +
              std::to_string(static_cast<std::int64_t>(expected_bits)) + ", packed=" +
              std::to_string(static_cast<std::int64_t>(actual_bits)) + ")");
}

void test_shape(const std::string& name, const LaneShape& shape, Opcode expected,
                const std::string& type, std::size_t width) {
    for (const std::size_t lanes : {std::size_t{2}, std::size_t{4}, std::size_t{8}}) {
        if (lanes * width < 16U) continue;
        const auto entry = name + std::to_string(lanes);
        run_case(name + " over " + std::to_string(lanes) + " " + type + " lanes",
                 wrap(build(entry, shape, lanes, type, width), shape.sources), entry, expected);
    }
}

void test_two_source_map() {
    LaneShape shape;
    shape.sources = 2;
    shape.stages = {{"add", "l0", "l1"}};
    test_shape("map2_i32", shape, Opcode::binary_i32_contiguous_map2, "i32", 4);
    shape.stages = {{"sub", "l0", "l1"}};
    test_shape("map2_i64", shape, Opcode::binary_i64_contiguous_map2, "i64", 8);
}

void test_three_source_map() {
    LaneShape shape;
    shape.sources = 3;
    shape.stages = {{"xor", "l0", "l1"}, {"sub", "t0", "l2"}};
    test_shape("map3_i32", shape, Opcode::binary_i32_contiguous_map3, "i32", 4);
    test_shape("map3_i64", shape, Opcode::binary_i64_contiguous_map3, "i64", 8);
}

void test_chain() {
    LaneShape shape;
    shape.sources = 4;
    shape.stages = {{"add", "l0", "l1"}, {"xor", "t0", "l2"}, {"sub", "t1", "l3"}};
    test_shape("chain_i32", shape, Opcode::binary_i32_contiguous_chain, "i32", 4);
    test_shape("chain_i64", shape, Opcode::binary_i64_contiguous_chain, "i64", 8);
}

void test_dag() {
    LaneShape shape;
    shape.sources = 4;
    shape.stages = {{"add", "l0", "l1"}, {"xor", "l2", "l3"}, {"sub", "t0", "t1"}};
    test_shape("dag_i32", shape, Opcode::binary_i32_contiguous_dag, "i32", 4);
    test_shape("dag_i64", shape, Opcode::binary_i64_contiguous_dag, "i64", 8);
}

void test_reusable_dag() {
    // t0 feeds both t1 and t2, so the shared node must be materialized once.
    LaneShape shape;
    shape.sources = 3;
    shape.stages = {{"add", "l0", "l1"}, {"xor", "t0", "l2"}, {"sub", "t1", "t0"}};
    test_shape("reuse_i32", shape, Opcode::binary_i32_contiguous_dag_reuse, "i32", 4);
    test_shape("reuse_i64", shape, Opcode::binary_i64_contiguous_dag_reuse, "i64", 8);
}

// The contiguous reduction is recognized from a different shape: an add tree
// over one base pointer feeding a return. It needs an opaque pointer, so the
// entry point hands it a global.
void test_reduction() {
    const std::string source = R"(
module @x86_reduction {
  global @s0: i8[128] align 16 = zero
  func @sum(%p: ptr) -> i32 {
  entry:
    %p0 = ptr.offset ptr %p, 0
    %p1 = ptr.offset ptr %p, 4
    %p2 = ptr.offset ptr %p, 8
    %p3 = ptr.offset ptr %p, 12
    %a = load i32 %p0 align 4
    %b = load i32 %p1 align 4
    %c = load i32 %p2 align 4
    %d = load i32 %p3 align 4
    %ab = add i32 %a, %b
    %cd = add i32 %c, %d
    %r = add i32 %ab, %cd
    return %r
  }
  func @reduce_entry() -> i32 {
  entry:
    %b = global.address ptr @s0
    %q0 = ptr.offset ptr %b, 0
    %q1 = ptr.offset ptr %b, 4
    %q2 = ptr.offset ptr %b, 8
    %q3 = ptr.offset ptr %b, 12
    %v0 = const i32 11
    %v1 = const i32 22
    %v2 = const i32 33
    %v3 = const i32 44
    store i32 %v0, %q0 align 4
    store i32 %v1, %q1 align 4
    store i32 %v2, %q2 align 4
    store i32 %v3, %q3 align 4
    %r = call i32 @sum(%b)
    return %r
  }
}
)";
    run_case("a four-lane contiguous reduction", source, "reduce_entry",
             Opcode::reduce_add_i32_contiguous);
}

} // namespace

int main() {
    test_two_source_map();
    test_three_source_map();
    test_chain();
    test_dag();
    test_reusable_dag();
    test_reduction();
    if (failures != 0) {
        std::cerr << failures << " x86-64 packed differential checks failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "x86-64 packed differential tests passed\n";
    return EXIT_SUCCESS;
}
