// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

// AArch64 coverage for the packed machine pseudos that the first NEON slice
// left unencoded: contiguous reductions, arbitrary-depth chains, postfix and
// reusable expression DAGs, and packed spill/reload. These pseudos are built
// directly as machine IR because no front end produces all of them, and the
// assertions check emitted instruction words so an encoding regression is
// caught rather than merely a "still compiles" result.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "forge/codegen/aarch64/encoder.hpp"
#include "forge/codegen/aarch64/register_allocation.hpp"
#include "forge/ir/parser.hpp"
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

using forge::machine::Block;
using forge::machine::Function;
using forge::machine::Instruction;
using forge::machine::Opcode;
using forge::machine::RegisterClass;
using forge::machine::VirtualRegister;

std::vector<std::uint32_t> words(const std::vector<std::byte>& code) {
    std::vector<std::uint32_t> result;
    for (std::size_t at = 0; at + 3U < code.size(); at += 4U) {
        std::uint32_t word = 0;
        for (unsigned byte = 0; byte < 4U; ++byte)
            word |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(code[at + byte])) << (byte * 8U);
        result.push_back(word);
    }
    return result;
}

std::size_t count_masked(const std::vector<std::uint32_t>& stream, std::uint32_t mask, std::uint32_t value) {
    return static_cast<std::size_t>(std::count_if(stream.begin(), stream.end(),
        [&](std::uint32_t word) { return (word & mask) == value; }));
}

// A64 instruction shapes the assertions below look for.
constexpr std::uint32_t addv_4s_mask = 0xFFFFFC00U, addv_4s = 0x4EB1B800U;
constexpr std::uint32_t addp_2d_mask = 0xFFFFFC00U, addp_2d = 0x5EF1B800U;
constexpr std::uint32_t fmov_w_s_mask = 0xFFFFFC00U, fmov_w_s = 0x1E260000U;
constexpr std::uint32_t fmov_x_d_mask = 0xFFFFFC00U, fmov_x_d = 0x9E660000U;
constexpr std::uint32_t vector_form_mask = 0xFFC00000U;
constexpr std::uint32_t ldr_q_scaled = 0x3DC00000U, str_q_scaled = 0x3D800000U;
constexpr std::uint32_t ldur_q = 0x3CC00000U, stur_q = 0x3C800000U;
constexpr std::uint32_t ldr_d_scaled = 0xFD400000U, str_d_scaled = 0xFD000000U;
constexpr std::uint32_t ldur_d = 0xFC400000U, stur_d = 0xFC000000U;
constexpr std::uint32_t neon_binary_mask = 0xFFE0FC00U;
constexpr std::uint32_t neon_add_2d = 0x4EE08400U, neon_add_4s = 0x4EA08400U;
constexpr std::uint32_t neon_sub_2d = 0x6EE08400U, neon_sub_4s = 0x6EA08400U, neon_eor_16b = 0x6E201C00U;
constexpr std::uint32_t neon_mov_16b = 0x4EA01C00U;
constexpr std::uint32_t neon_fadd_4s = 0x4E20D400U, neon_fadd_2d = 0x4E60D400U;
constexpr std::uint32_t neon_fsub_4s = 0x4EA0D400U, neon_fsub_2d = 0x4EE0D400U;
constexpr std::uint32_t neon_fmul_4s = 0x6E20DC00U, neon_fmul_2d = 0x6E60DC00U;
constexpr std::uint32_t neon_fdiv_4s = 0x6E20FC00U, neon_fdiv_2d = 0x6E60FC00U;

std::string metadata16(std::initializer_list<std::uint16_t> tokens) {
    std::string blob;
    for (const auto token : tokens) {
        blob.push_back(static_cast<char>(token & 0xFFU));
        blob.push_back(static_cast<char>((token >> 8U) & 0xFFU));
    }
    return blob;
}

// A function taking `pointer_count` pointer arguments, each already loaded into
// its own virtual register, so a packed pseudo has real operands to address.
Function pointer_function(const std::string& name, std::uint32_t pointer_count) {
    Function function;
    function.name = name;
    function.register_count = pointer_count;
    function.argument_count = pointer_count;
    Block entry;
    entry.name = "entry";
    for (std::uint32_t index = 0; index < pointer_count; ++index) {
        function.register_widths.push_back(8U);
        function.register_classes.push_back(RegisterClass::integer);
        function.argument_widths.push_back(8U);
        function.argument_classes.push_back(RegisterClass::integer);
        Instruction load;
        load.opcode = Opcode::load_argument_i64;
        load.result = index;
        load.argument_index = index;
        entry.instructions.push_back(std::move(load));
    }
    function.blocks.push_back(std::move(entry));
    return function;
}

void terminate_void(Function& function) {
    Instruction ret;
    ret.opcode = Opcode::return_void;
    function.blocks.back().instructions.push_back(std::move(ret));
}

// Encodes a single-function module and returns its instruction words. Runs the
// shared machine verifier first, so an operand-count rule that does not know
// about these pseudos fails here rather than silently skipping the test.
std::vector<std::uint32_t> encode_words(Function function, const std::string& what) {
    forge::machine::Module module;
    module.functions.push_back(std::move(function));
    const auto verification = forge::machine::verify_module(module);
    check(verification.empty(), what + " passes the machine verifier");
    for (const auto& diagnostic : verification) std::cerr << "  verifier: " << diagnostic.message << '\n';
    auto encoded = forge::codegen::aarch64::encode(module);
    check(encoded.ok(), what + " encodes for AArch64");
    for (const auto& diagnostic : encoded.diagnostics) std::cerr << "  encoder: " << diagnostic.message << '\n';
    if (!encoded.ok() || encoded.functions.empty()) return {};
    const auto& code = encoded.functions.front().code;
    check(!code.empty() && (code.size() % 4U) == 0U, what + " emits whole A64 words");
    return words(code);
}

void test_i32_reduction() {
    auto function = pointer_function("reduce_i32", 1U);
    function.register_count = 2U;
    function.register_widths.push_back(4U);
    function.register_classes.push_back(RegisterClass::integer);
    Instruction reduce;
    reduce.opcode = Opcode::reduce_add_i32_contiguous;
    reduce.result = 1U;
    reduce.inputs.push_back(0U);
    reduce.immediate = 4;
    reduce.vector_bits = 128U;
    function.blocks.back().instructions.push_back(std::move(reduce));
    Instruction ret;
    ret.opcode = Opcode::return_i32;
    ret.inputs.push_back(1U);
    function.blocks.back().instructions.push_back(std::move(ret));

    const auto stream = encode_words(std::move(function), "four-lane i32 reduction");
    if (stream.empty()) return;
    // Four i32 lanes are exactly one vector, so a single load feeds one ADDV.
    check(count_masked(stream, vector_form_mask, ldr_q_scaled) +
          count_masked(stream, vector_form_mask, ldur_q) == 1U,
          "four-lane i32 reduction loads exactly one 128-bit vector");
    check(count_masked(stream, addv_4s_mask, addv_4s) == 1U,
          "four-lane i32 reduction uses ADDV for the horizontal add");
    check(count_masked(stream, fmov_w_s_mask, fmov_w_s) == 1U,
          "i32 reduction moves the scalar result back with FMOV w, s");
}

void test_i64_reduction_uses_two_accumulators() {
    auto function = pointer_function("reduce_i64", 1U);
    function.register_count = 2U;
    function.register_widths.push_back(8U);
    function.register_classes.push_back(RegisterClass::integer);
    Instruction reduce;
    reduce.opcode = Opcode::reduce_add_i64_contiguous;
    reduce.result = 1U;
    reduce.inputs.push_back(0U);
    reduce.immediate = 8; // 64 bytes: four vectors.
    reduce.vector_bits = 128U;
    function.blocks.back().instructions.push_back(std::move(reduce));
    Instruction ret;
    ret.opcode = Opcode::return_i64;
    ret.inputs.push_back(1U);
    function.blocks.back().instructions.push_back(std::move(ret));

    const auto stream = encode_words(std::move(function), "eight-lane i64 reduction");
    if (stream.empty()) return;
    check(count_masked(stream, vector_form_mask, ldr_q_scaled) +
          count_masked(stream, vector_form_mask, ldur_q) == 4U,
          "eight-lane i64 reduction loads four 128-bit vectors");
    // Two independent accumulators plus the join: three vector adds, not four.
    check(count_masked(stream, neon_binary_mask, neon_add_2d) == 3U,
          "i64 reduction splits the add chain across two accumulators");
    check(count_masked(stream, addp_2d_mask, addp_2d) == 1U,
          "i64 reduction uses scalar pairwise ADDP, which has no ADDV form");
    check(count_masked(stream, fmov_x_d_mask, fmov_x_d) == 1U,
          "i64 reduction moves the scalar result back with FMOV x, d");
}

void test_vector_chain() {
    // d[i] = ((a[i] + b[i]) ^ c[i]) - e[i] over four i64 lanes: two vectors.
    auto function = pointer_function("chain_i64", 5U);
    Instruction chain;
    chain.opcode = Opcode::binary_i64_contiguous_chain;
    chain.inputs = {0U, 1U, 2U, 3U, 4U};
    chain.symbol = metadata16({static_cast<std::uint16_t>(Opcode::add_i64),
                               static_cast<std::uint16_t>(Opcode::xor_i64),
                               static_cast<std::uint16_t>(Opcode::sub_i64)});
    chain.immediate = 4;
    chain.vector_bits = 128U;
    function.blocks.back().instructions.push_back(std::move(chain));
    terminate_void(function);

    const auto stream = encode_words(std::move(function), "four-lane i64 chain");
    if (stream.empty()) return;
    check(count_masked(stream, vector_form_mask, str_q_scaled) +
          count_masked(stream, vector_form_mask, stur_q) == 2U,
          "four-lane i64 chain stores two 128-bit vectors");
    check(count_masked(stream, neon_binary_mask, neon_add_2d) == 2U,
          "each i64 chain chunk performs its leading vector add");
    check(count_masked(stream, neon_binary_mask, neon_eor_16b) == 2U,
          "each i64 chain chunk performs its vector exclusive-or");
}

void test_chain_half_vector_tail() {
    // Two i32 lanes are exactly one D register. NEON has no predicate mask, so
    // Forge uses a narrow SIMD load/store for the active half of the Q register
    // rather than issuing eight separate scalar source loads.
    auto function = pointer_function("chain_i32_tail", 5U);
    Instruction chain;
    chain.opcode = Opcode::binary_i32_contiguous_chain;
    chain.inputs = {0U, 1U, 2U, 3U, 4U};
    chain.symbol = metadata16({static_cast<std::uint16_t>(Opcode::add_i32),
                               static_cast<std::uint16_t>(Opcode::and_i32),
                               static_cast<std::uint16_t>(Opcode::or_i32)});
    chain.immediate = 2;
    chain.vector_bits = 128U;
    function.blocks.back().instructions.push_back(std::move(chain));
    terminate_void(function);

    const auto stream = encode_words(std::move(function), "two-lane i32 chain");
    if (stream.empty()) return;
    check(count_masked(stream, vector_form_mask, ldr_q_scaled) +
          count_masked(stream, vector_form_mask, ldur_q) == 0U,
          "a half-vector chain emits no 128-bit loads");
    check(count_masked(stream, vector_form_mask, ldr_d_scaled) +
          count_masked(stream, vector_form_mask, ldur_d) == 4U,
          "a half-vector chain loads each source once through a D register");
    check(count_masked(stream, vector_form_mask, str_d_scaled) +
          count_masked(stream, vector_form_mask, stur_d) == 1U,
          "a half-vector chain stores exactly the active 64-bit tail");
    check(count_masked(stream, neon_binary_mask, neon_add_4s) == 1U,
          "a half-vector i32 tail remains in NEON for arithmetic");
}

void test_postfix_dag() {
    // d[i] = (a[i] + b[i]) - (a[i] ^ c[i]) over four i32 lanes.
    auto function = pointer_function("dag_i32", 4U);
    Instruction dag;
    dag.opcode = Opcode::binary_i32_contiguous_dag;
    dag.inputs = {0U, 1U, 2U, 3U};
    dag.symbol = metadata16({0x8000U, 0x8001U, static_cast<std::uint16_t>(Opcode::add_i32),
                             0x8000U, 0x8002U, static_cast<std::uint16_t>(Opcode::xor_i32),
                             static_cast<std::uint16_t>(Opcode::sub_i32)});
    dag.immediate = 4;
    dag.vector_bits = 128U;
    function.blocks.back().instructions.push_back(std::move(dag));
    terminate_void(function);

    const auto stream = encode_words(std::move(function), "four-lane i32 postfix DAG");
    if (stream.empty()) return;
    check(count_masked(stream, vector_form_mask, ldr_q_scaled) +
          count_masked(stream, vector_form_mask, ldur_q) == 4U,
          "the postfix DAG loads each of its four source operands once");
    check(count_masked(stream, neon_binary_mask, neon_add_4s) == 1U &&
          count_masked(stream, neon_binary_mask, neon_eor_16b) == 1U &&
          count_masked(stream, neon_binary_mask, neon_sub_4s) == 1U,
          "the postfix DAG evaluates each interior node exactly once");
}

void test_reusable_dag_materializes_shared_node_once() {
    // node2 = a + b is consumed by both node4 and node5, so a correct evaluator
    // computes it once and copies it aside rather than recomputing the subtree.
    auto function = pointer_function("dag_reuse_i64", 4U);
    std::string blob;
    const auto node = [&](std::uint16_t tag, std::uint16_t lhs, std::uint16_t rhs) {
        for (const auto word : {tag, lhs, rhs}) {
            blob.push_back(static_cast<char>(word & 0xFFU));
            blob.push_back(static_cast<char>((word >> 8U) & 0xFFU));
        }
    };
    node(0x8000U, 0U, 0U);
    node(0x8001U, 0U, 0U);
    node(static_cast<std::uint16_t>(Opcode::add_i64), 0U, 1U);
    node(0x8002U, 0U, 0U);
    node(static_cast<std::uint16_t>(Opcode::xor_i64), 2U, 3U);
    node(static_cast<std::uint16_t>(Opcode::sub_i64), 4U, 2U);

    Instruction dag;
    dag.opcode = Opcode::binary_i64_contiguous_dag_reuse;
    dag.inputs = {0U, 1U, 2U, 3U};
    dag.symbol = blob;
    dag.immediate = 2; // One 128-bit chunk.
    dag.vector_bits = 128U;
    function.blocks.back().instructions.push_back(std::move(dag));
    terminate_void(function);

    const auto stream = encode_words(std::move(function), "reusable i64 DAG");
    if (stream.empty()) return;
    check(count_masked(stream, neon_binary_mask, neon_add_2d) == 1U,
          "the shared DAG node is computed once instead of rematerialized");
    check(count_masked(stream, vector_form_mask, ldr_q_scaled) +
          count_masked(stream, vector_form_mask, ldur_q) == 3U,
          "the reusable DAG loads each distinct source once per chunk");
    // A64 has a true three-operand SIMD form, so retaining the shared value does
    // not require the copy that a destructive two-operand evaluator would need.
    check(count_masked(stream, neon_binary_mask, neon_sub_2d) == 1U &&
          count_masked(stream, neon_binary_mask, neon_eor_16b) == 1U,
          "the reusable DAG consumes its shared node with three-operand NEON operations");
    check(count_masked(stream, neon_binary_mask, neon_mov_16b) == 0U,
          "the reusable DAG avoids a redundant vector copy");
}

void test_packed_spill_reload() {
    // A 256-bit packed value moved between two frame homes. AArch64 has no
    // 256-bit register, so this must lower to two 128-bit moves per transfer.
    Function function;
    function.name = "packed_spill";
    function.register_count = 1U;
    function.register_widths = {32U};
    function.register_classes = {RegisterClass::vector};
    function.local_stack_size = 64U;
    Block entry;
    entry.name = "entry";
    Instruction load;
    load.opcode = Opcode::load_stack_v256;
    load.result = 0U;
    load.immediate = -32;
    load.vector_bits = 256U;
    entry.instructions.push_back(std::move(load));
    Instruction store;
    store.opcode = Opcode::store_stack_v256;
    store.inputs.push_back(0U);
    store.immediate = -64;
    store.vector_bits = 256U;
    entry.instructions.push_back(std::move(store));
    Instruction ret;
    ret.opcode = Opcode::return_void;
    entry.instructions.push_back(std::move(ret));
    function.blocks.push_back(std::move(entry));

    const auto allocation = forge::codegen::aarch64::allocate_registers(function);
    check(allocation.ok(), "packed value allocation succeeds");
    if (allocation.ok()) {
        // AAPCS64 preserves only the low half of v8-v15, so a packed value must
        // never be handed one of them.
        check(allocation.location(0U).kind == forge::codegen::aarch64::AllocationKind::stack_slot,
              "a packed value is stack-homed rather than parked in v8-v15");
        check(!allocation.spill_slot_sizes.empty() && allocation.spill_slot_sizes.front() == 32U,
              "a 256-bit packed value reserves a 32-byte spill slot");
        check(allocation.spill_bytes >= 32U, "the spill area covers the packed slot in full");
    }

    const auto stream = encode_words(std::move(function), "256-bit packed spill and reload");
    if (stream.empty()) return;
    const auto loads = count_masked(stream, vector_form_mask, ldr_q_scaled) +
                       count_masked(stream, vector_form_mask, ldur_q);
    const auto stores = count_masked(stream, vector_form_mask, str_q_scaled) +
                        count_masked(stream, vector_form_mask, stur_q);
    check(loads == 4U && stores == 4U,
          "each 256-bit transfer becomes two 128-bit moves, in both directions");
}

void test_frame_access_uses_a_single_instruction() {
    // Frame slots are small negative displacements, which the unscaled LDUR and
    // STUR forms reach directly. Materializing the address in a scratch register
    // instead would show up as extra ADD/SUB words.
    Function function;
    function.name = "frame";
    function.register_count = 1U;
    function.register_widths = {8U};
    function.register_classes = {RegisterClass::integer};
    function.local_stack_size = 32U;
    Block entry;
    entry.name = "entry";
    Instruction load;
    load.opcode = Opcode::load_stack_i64;
    load.result = 0U;
    load.immediate = -8;
    entry.instructions.push_back(std::move(load));
    Instruction store;
    store.opcode = Opcode::store_stack_i64;
    store.inputs.push_back(0U);
    store.immediate = -16;
    entry.instructions.push_back(std::move(store));
    Instruction ret;
    ret.opcode = Opcode::return_void;
    entry.instructions.push_back(std::move(ret));
    function.blocks.push_back(std::move(entry));

    const auto stream = encode_words(std::move(function), "frame slot access");
    if (stream.empty()) return;
    check(count_masked(stream, 0xFFE00C00U, 0xF8400000U) >= 1U,
          "a negative frame load selects the unscaled LDUR form");
    check(count_masked(stream, 0xFFE00C00U, 0xF8000000U) >= 1U,
          "a negative frame store selects the unscaled STUR form");
    // Only the prologue's frame adjustment and the epilogue's restore should
    // need an ADD/SUB immediate; no memory access may add its own.
    check(count_masked(stream, 0x7F800000U, 0x11000000U) +
          count_masked(stream, 0x7F800000U, 0x51000000U) <= 3U,
          "addressed frame slots do not materialize their address separately");
}

// --- Automatic reduction formation ------------------------------------------
//
// The canonical AArch64 pass folds a straight-line add tree over a contiguous
// run of loads into the reduction pseudo. These check both halves of that: the
// shapes it must recognize, and the shapes it must leave alone.

const forge::machine::Function* lower_one(const std::string& source, const std::string& name,
                                          forge::machine::LowerResult& storage, const std::string& what) {
    auto parsed = forge::ir::parse_module(source);
    check(parsed.ok(), what + " parses");
    if (!parsed.ok()) return nullptr;
    storage = forge::machine::lower_module(*parsed.module, {forge::machine::TargetArchitecture::aarch64});
    check(storage.ok(), what + " lowers for AArch64");
    if (!storage.ok()) {
        for (const auto& diagnostic : storage.diagnostics) std::cerr << "  lowering: " << diagnostic.message << '\n';
        return nullptr;
    }
    for (const auto& function : storage.module->functions)
        if (function.name == name) return &function;
    check(false, what + " produces @" + name);
    return nullptr;
}

const Instruction* find_opcode(const Function& function, Opcode opcode) {
    for (const auto& block : function.blocks)
        for (const auto& instruction : block.instructions)
            if (instruction.opcode == opcode) return &instruction;
    return nullptr;
}

std::size_t count_opcode(const Function& function, Opcode opcode) {
    std::size_t total = 0;
    for (const auto& block : function.blocks)
        for (const auto& instruction : block.instructions)
            if (instruction.opcode == opcode) ++total;
    return total;
}

void test_floating_opcode_matrix() {
    struct Case { Opcode operation; bool wide; std::uint32_t encoding; const char* name; };
    const Case cases[] = {
        {Opcode::add_f32, false, neon_fadd_4s, "f32 add"},
        {Opcode::sub_f32, false, neon_fsub_4s, "f32 sub"},
        {Opcode::mul_f32, false, neon_fmul_4s, "f32 mul"},
        {Opcode::div_f32, false, neon_fdiv_4s, "f32 div"},
        {Opcode::add_f64, true, neon_fadd_2d, "f64 add"},
        {Opcode::sub_f64, true, neon_fsub_2d, "f64 sub"},
        {Opcode::mul_f64, true, neon_fmul_2d, "f64 mul"},
        {Opcode::div_f64, true, neon_fdiv_2d, "f64 div"},
    };
    for (const auto& entry : cases) {
        auto function = pointer_function(std::string("packed_") + entry.name, 3U);
        Instruction packed;
        packed.opcode = entry.wide ? Opcode::binary_i64_contiguous_map2 : Opcode::binary_i32_contiguous_map2;
        packed.inputs = {0U, 1U, 2U};
        packed.immediate = entry.wide ? 2 : 4;
        packed.argument_index = static_cast<std::uint32_t>(entry.operation);
        packed.vector_bits = 128U;
        function.blocks.back().instructions.push_back(std::move(packed));
        terminate_void(function);
        const auto stream = encode_words(std::move(function), entry.name);
        if (stream.empty()) continue;
        check(count_masked(stream, neon_binary_mask, entry.encoding) == 1U,
              std::string("AArch64 emits native NEON ") + entry.name);
    }
}

void test_floating_expression_slp() {
    const std::string scalar_source = R"(
module @float_scalar_slp {
  global @a: i8[64] align 16 = zero
  global @dst: i8[64] align 16 = zero
  func @scale4(%scale: f32) -> void {
  entry:
    %abase = global.address ptr @a
    %dbase = global.address ptr @dst
    %a0p = ptr.offset ptr %abase, 0
    %d0p = ptr.offset ptr %dbase, 0
    %a0 = load f32 %a0p align 4
    %r0 = mul f32 %a0, %scale
    store f32 %r0, %d0p align 4
    %a1p = ptr.offset ptr %abase, 4
    %d1p = ptr.offset ptr %dbase, 4
    %a1 = load f32 %a1p align 4
    %r1 = mul f32 %a1, %scale
    store f32 %r1, %d1p align 4
    %a2p = ptr.offset ptr %abase, 8
    %d2p = ptr.offset ptr %dbase, 8
    %a2 = load f32 %a2p align 4
    %r2 = mul f32 %a2, %scale
    store f32 %r2, %d2p align 4
    %a3p = ptr.offset ptr %abase, 12
    %d3p = ptr.offset ptr %dbase, 12
    %a3 = load f32 %a3p align 4
    %r3 = mul f32 %a3, %scale
    store f32 %r3, %d3p align 4
    return
  }
}
)";
    forge::machine::LowerResult scalar_lowered;
    const auto* scalar_function = lower_one(scalar_source, "scale4", scalar_lowered, "four-lane f32 scalar map");
    if (scalar_function != nullptr) {
        const auto* packed = find_opcode(*scalar_function, Opcode::binary_i32_contiguous_map);
        check(packed != nullptr && static_cast<Opcode>(packed->argument_index) == Opcode::mul_f32,
              "AArch64 forms a packed f32 scalar-broadcast map");
        auto encoded = forge::codegen::aarch64::encode(*scalar_lowered.module);
        check(encoded.ok(), "packed f32 scalar map encodes for AArch64");
        if (encoded.ok() && !encoded.functions.empty()) {
            const auto stream = words(encoded.functions.front().code);
            check(count_masked(stream, neon_binary_mask, neon_fmul_4s) == 1U,
                  "packed f32 scalar map emits FMUL v.4s");
        }
    }

    const std::string f32_source = R"(
module @float_slp32 {
  global @a: i8[64] align 16 = zero
  global @b: i8[64] align 16 = zero
  global @dst: i8[64] align 16 = zero
  func @map4() -> void {
  entry:
    %abase = global.address ptr @a
    %bbase = global.address ptr @b
    %dbase = global.address ptr @dst
    %a0p = ptr.offset ptr %abase, 0
    %b0p = ptr.offset ptr %bbase, 0
    %d0p = ptr.offset ptr %dbase, 0
    %a0 = load f32 %a0p align 4
    %b0 = load f32 %b0p align 4
    %r0 = add f32 %a0, %b0
    store f32 %r0, %d0p align 4
    %a1p = ptr.offset ptr %abase, 4
    %b1p = ptr.offset ptr %bbase, 4
    %d1p = ptr.offset ptr %dbase, 4
    %a1 = load f32 %a1p align 4
    %b1 = load f32 %b1p align 4
    %r1 = add f32 %a1, %b1
    store f32 %r1, %d1p align 4
    %a2p = ptr.offset ptr %abase, 8
    %b2p = ptr.offset ptr %bbase, 8
    %d2p = ptr.offset ptr %dbase, 8
    %a2 = load f32 %a2p align 4
    %b2 = load f32 %b2p align 4
    %r2 = add f32 %a2, %b2
    store f32 %r2, %d2p align 4
    %a3p = ptr.offset ptr %abase, 12
    %b3p = ptr.offset ptr %bbase, 12
    %d3p = ptr.offset ptr %dbase, 12
    %a3 = load f32 %a3p align 4
    %b3 = load f32 %b3p align 4
    %r3 = add f32 %a3, %b3
    store f32 %r3, %d3p align 4
    return
  }
}
)";
    forge::machine::LowerResult lowered32;
    const auto* function32 = lower_one(f32_source, "map4", lowered32, "four-lane f32 expression SLP");
    if (function32 != nullptr) {
        const auto* packed = find_opcode(*function32, Opcode::binary_i32_contiguous_map2);
        check(packed != nullptr, "AArch64 forms a packed f32 map2 expression");
        if (packed != nullptr) {
            check(static_cast<Opcode>(packed->argument_index) == Opcode::add_f32,
                  "the packed f32 map records floating add semantics");
            check(packed->immediate == 4, "the packed f32 map covers one complete Q register");
        }
        check(count_opcode(*function32, Opcode::add_f32) == 0U,
              "f32 SLP removes the scalar add instructions");
        auto encoded = forge::codegen::aarch64::encode(*lowered32.module);
        check(encoded.ok(), "packed f32 map encodes for AArch64");
        if (encoded.ok() && !encoded.functions.empty()) {
            const auto stream = words(encoded.functions.front().code);
            check(count_masked(stream, neon_binary_mask, neon_fadd_4s) == 1U,
                  "packed f32 map emits FADD v.4s");
        }
    }

    const std::string f64_source = R"(
module @float_slp64 {
  global @a: i8[64] align 16 = zero
  global @b: i8[64] align 16 = zero
  global @c: i8[64] align 16 = zero
  global @dst: i8[64] align 16 = zero
  func @map2d() -> void {
  entry:
    %abase = global.address ptr @a
    %bbase = global.address ptr @b
    %cbase = global.address ptr @c
    %dbase = global.address ptr @dst
    %a0p = ptr.offset ptr %abase, 0
    %b0p = ptr.offset ptr %bbase, 0
    %c0p = ptr.offset ptr %cbase, 0
    %d0p = ptr.offset ptr %dbase, 0
    %a0 = load f64 %a0p align 8
    %b0 = load f64 %b0p align 8
    %c0 = load f64 %c0p align 8
    %s0 = add f64 %a0, %b0
    %r0 = mul f64 %s0, %c0
    store f64 %r0, %d0p align 8
    %a1p = ptr.offset ptr %abase, 8
    %b1p = ptr.offset ptr %bbase, 8
    %c1p = ptr.offset ptr %cbase, 8
    %d1p = ptr.offset ptr %dbase, 8
    %a1 = load f64 %a1p align 8
    %b1 = load f64 %b1p align 8
    %c1 = load f64 %c1p align 8
    %s1 = add f64 %a1, %b1
    %r1 = mul f64 %s1, %c1
    store f64 %r1, %d1p align 8
    return
  }
}
)";
    forge::machine::LowerResult lowered64;
    const auto* function64 = lower_one(f64_source, "map2d", lowered64, "two-lane f64 chained SLP");
    if (function64 != nullptr) {
        const auto* packed = find_opcode(*function64, Opcode::binary_i64_contiguous_map3);
        check(packed != nullptr, "AArch64 forms a packed two-operation f64 map");
        if (packed != nullptr) {
            check(static_cast<Opcode>(packed->argument_index & 0xffffU) == Opcode::add_f64 &&
                  static_cast<Opcode>((packed->argument_index >> 16U) & 0xffffU) == Opcode::mul_f64,
                  "the packed f64 map retains add-then-multiply semantics");
        }
        auto encoded = forge::codegen::aarch64::encode(*lowered64.module);
        check(encoded.ok(), "packed f64 chained map encodes for AArch64");
        if (encoded.ok() && !encoded.functions.empty()) {
            const auto stream = words(encoded.functions.front().code);
            check(count_masked(stream, neon_binary_mask, neon_fadd_2d) == 1U,
                  "packed f64 map emits FADD v.2d");
            check(count_masked(stream, neon_binary_mask, neon_fmul_2d) == 1U,
                  "packed f64 map emits FMUL v.2d");
        }
    }

    const auto build_tail_map = [](std::string_view module_name, std::string_view function_name,
                                   std::string_view type, std::size_t lanes) {
        const auto lane_bytes = type == "f64" ? 8U : 4U;
        std::string source = "module @" + std::string(module_name) + " {\n"
            "  global @a: i8[64] align 16 = zero\n"
            "  global @b: i8[64] align 16 = zero\n"
            "  global @dst: i8[64] align 16 = zero\n"
            "  func @" + std::string(function_name) + "() -> void {\n"
            "  entry:\n"
            "    %abase = global.address ptr @a\n"
            "    %bbase = global.address ptr @b\n"
            "    %dbase = global.address ptr @dst\n";
        for (std::size_t lane = 0; lane < lanes; ++lane) {
            const auto suffix = std::to_string(lane);
            const auto offset = std::to_string(lane * lane_bytes);
            source += "    %ap" + suffix + " = ptr.offset ptr %abase, " + offset + "\n";
            source += "    %bp" + suffix + " = ptr.offset ptr %bbase, " + offset + "\n";
            source += "    %dp" + suffix + " = ptr.offset ptr %dbase, " + offset + "\n";
            source += "    %a" + suffix + " = load " + std::string(type) + " %ap" + suffix + " align " + std::to_string(lane_bytes) + "\n";
            source += "    %b" + suffix + " = load " + std::string(type) + " %bp" + suffix + " align " + std::to_string(lane_bytes) + "\n";
            source += "    %r" + suffix + " = add " + std::string(type) + " %a" + suffix + ", %b" + suffix + "\n";
            source += "    store " + std::string(type) + " %r" + suffix + ", %dp" + suffix + " align " + std::to_string(lane_bytes) + "\n";
        }
        source += "    return\n  }\n}\n";
        return source;
    };

    {
        forge::machine::LowerResult lowered;
        const auto source = build_tail_map("float_tail32", "map6", "f32", 6U);
        const auto* function = lower_one(source, "map6", lowered, "six-lane f32 tail SLP");
        if (function != nullptr) {
            const auto* packed = find_opcode(*function, Opcode::binary_i32_contiguous_map2);
            check(packed != nullptr && packed->immediate == 6,
                  "AArch64 keeps a six-lane f32 run in one packed pseudo");
            auto encoded = forge::codegen::aarch64::encode(*lowered.module);
            check(encoded.ok(), "six-lane f32 tail pack encodes for AArch64");
            if (encoded.ok() && !encoded.functions.empty()) {
                const auto stream = words(encoded.functions.front().code);
                check(count_masked(stream, neon_binary_mask, neon_fadd_4s) == 2U,
                      "six-lane f32 pack uses one Q add and one D-tail add");
                check(count_masked(stream, vector_form_mask, str_d_scaled) +
                      count_masked(stream, vector_form_mask, stur_d) == 1U,
                      "six-lane f32 pack stores its two-lane tail through D");
            }
        }
    }

    {
        forge::machine::LowerResult lowered;
        const auto source = build_tail_map("float_tail64", "map3", "f64", 3U);
        const auto* function = lower_one(source, "map3", lowered, "three-lane f64 tail SLP");
        if (function != nullptr) {
            const auto* packed = find_opcode(*function, Opcode::binary_i64_contiguous_map2);
            check(packed != nullptr && packed->immediate == 3,
                  "AArch64 keeps a three-lane f64 run in one packed pseudo");
            auto encoded = forge::codegen::aarch64::encode(*lowered.module);
            check(encoded.ok(), "three-lane f64 tail pack encodes for AArch64");
            if (encoded.ok() && !encoded.functions.empty()) {
                const auto stream = words(encoded.functions.front().code);
                check(count_masked(stream, neon_binary_mask, neon_fadd_2d) == 2U,
                      "three-lane f64 pack uses one Q add and one D-tail add");
                check(count_masked(stream, vector_form_mask, str_d_scaled) +
                      count_masked(stream, vector_form_mask, stur_d) == 1U,
                      "three-lane f64 pack stores its one-lane tail through D");
            }
        }
    }
}

// Four contiguous i32 lanes summed as a balanced tree, the canonical shape.
const char* const balanced_source = R"(
module @reduction_balanced {
  func @sum4(%p: ptr) -> i32 {
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
}
)";

void test_reduction_is_formed_and_wired_correctly() {
    forge::machine::LowerResult lowered;
    const auto* function = lower_one(balanced_source, "sum4", lowered, "a four-lane i32 reduction");
    if (function == nullptr) return;

    const auto* reduce = find_opcode(*function, Opcode::reduce_add_i32_contiguous);
    check(reduce != nullptr, "the canonical AArch64 pass forms a contiguous i32 reduction");
    if (reduce == nullptr) return;
    check(reduce->immediate == 4, "the formed reduction records its four lanes");
    check(reduce->inputs.size() == 1U, "the formed reduction takes only the run's base pointer");

    const auto* argument = find_opcode(*function, Opcode::load_argument_i64);
    check(argument != nullptr && !reduce->inputs.empty() && reduce->inputs.front() == argument->result,
          "the reduction reads from the pointer argument the lanes were offset from");

    // The scalar tree must be gone, not merely joined by a packed instruction.
    check(count_opcode(*function, Opcode::load_ptr_i32) == 0U,
          "forming the reduction dissolves every scalar lane load");
    check(count_opcode(*function, Opcode::add_i32) == 0U,
          "forming the reduction dissolves every scalar add");

    // Regression guard: the pass compacts the virtual register file after
    // forming a packed instruction, so any later rewrite keyed on the old
    // numbering silently retargets operands. That produced a function that
    // returned its own base pointer instead of the sum.
    const auto* ret = find_opcode(*function, Opcode::return_i32);
    check(ret != nullptr && ret->inputs.size() == 1U && ret->inputs.front() == reduce->result,
          "the return still reads the reduction result after register compaction");
}

void test_reduction_shapes_that_must_be_recognized() {
    struct Case {
        const char* what;
        const char* name;
        Opcode opcode;
        std::int64_t lanes;
        const char* source;
    };
    const Case cases[] = {
        {"a left-leaning add chain", "linear4", Opcode::reduce_add_i32_contiguous, 4, R"(
module @reduction_linear {
  func @linear4(%p: ptr) -> i32 {
  entry:
    %p0 = ptr.offset ptr %p, 0
    %p1 = ptr.offset ptr %p, 4
    %p2 = ptr.offset ptr %p, 8
    %p3 = ptr.offset ptr %p, 12
    %a = load i32 %p0 align 4
    %b = load i32 %p1 align 4
    %c = load i32 %p2 align 4
    %d = load i32 %p3 align 4
    %s1 = add i32 %a, %b
    %s2 = add i32 %s1, %c
    %s3 = add i32 %s2, %d
    return %s3
  }
}
)"},
        {"eight i64 lanes", "sum8", Opcode::reduce_add_i64_contiguous, 8, R"(
module @reduction_wide {
  func @sum8(%p: ptr) -> i64 {
  entry:
    %p0 = ptr.offset ptr %p, 0
    %p1 = ptr.offset ptr %p, 8
    %p2 = ptr.offset ptr %p, 16
    %p3 = ptr.offset ptr %p, 24
    %p4 = ptr.offset ptr %p, 32
    %p5 = ptr.offset ptr %p, 40
    %p6 = ptr.offset ptr %p, 48
    %p7 = ptr.offset ptr %p, 56
    %a = load i64 %p0 align 8
    %b = load i64 %p1 align 8
    %c = load i64 %p2 align 8
    %d = load i64 %p3 align 8
    %e = load i64 %p4 align 8
    %f = load i64 %p5 align 8
    %g = load i64 %p6 align 8
    %h = load i64 %p7 align 8
    %ab = add i64 %a, %b
    %cd = add i64 %c, %d
    %ef = add i64 %e, %f
    %gh = add i64 %g, %h
    %abcd = add i64 %ab, %cd
    %efgh = add i64 %ef, %gh
    %r = add i64 %abcd, %efgh
    return %r
  }
}
)"},
        // The x86-64 recognizer only starts from a return operand; this one
        // starts from any root, so a sum consumed mid-function still packs.
        {"a sum consumed mid-function", "midfunction", Opcode::reduce_add_i32_contiguous, 4, R"(
module @reduction_mid {
  func @midfunction(%p: ptr) -> i32 {
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
    %sum = add i32 %ab, %cd
    %squared = mul i32 %sum, %sum
    return %squared
  }
}
)"},
    };

    for (const auto& entry : cases) {
        forge::machine::LowerResult lowered;
        const auto* function = lower_one(entry.source, entry.name, lowered, entry.what);
        if (function == nullptr) continue;
        const auto* reduce = find_opcode(*function, entry.opcode);
        check(reduce != nullptr, std::string("AArch64 forms a reduction for ") + entry.what);
        if (reduce == nullptr) continue;
        check(reduce->immediate == entry.lanes,
              std::string("the reduction for ") + entry.what + " records the right lane count");

        forge::machine::Module single;
        single.functions.push_back(*function);
        auto encoded = forge::codegen::aarch64::encode(single);
        check(encoded.ok(), std::string("the reduction for ") + entry.what + " encodes");
        if (!encoded.ok() || encoded.functions.empty()) continue;
        const auto stream = words(encoded.functions.front().code);
        const bool wide = entry.opcode == Opcode::reduce_add_i64_contiguous;
        check(count_masked(stream, wide ? addp_2d_mask : addv_4s_mask, wide ? addp_2d : addv_4s) == 1U,
              std::string("the reduction for ") + entry.what + " emits its horizontal add");
    }
}

void test_reduction_shapes_that_must_be_rejected() {
    struct Case { const char* why; const char* name; const char* source; };
    const Case cases[] = {
        {"a gap in the run", "gap4", R"(
module @reject_gap {
  func @gap4(%p: ptr) -> i32 {
  entry:
    %p0 = ptr.offset ptr %p, 0
    %p1 = ptr.offset ptr %p, 4
    %p2 = ptr.offset ptr %p, 12
    %p3 = ptr.offset ptr %p, 16
    %a = load i32 %p0 align 4
    %b = load i32 %p1 align 4
    %c = load i32 %p2 align 4
    %d = load i32 %p3 align 4
    %ab = add i32 %a, %b
    %cd = add i32 %c, %d
    %r = add i32 %ab, %cd
    return %r
  }
}
)"},
        {"lanes drawn from two different base pointers", "twobases", R"(
module @reject_bases {
  func @twobases(%p: ptr, %q: ptr) -> i32 {
  entry:
    %p0 = ptr.offset ptr %p, 0
    %p1 = ptr.offset ptr %p, 4
    %q2 = ptr.offset ptr %q, 8
    %q3 = ptr.offset ptr %q, 12
    %a = load i32 %p0 align 4
    %b = load i32 %p1 align 4
    %c = load i32 %q2 align 4
    %d = load i32 %q3 align 4
    %ab = add i32 %a, %b
    %cd = add i32 %c, %d
    %r = add i32 %ab, %cd
    return %r
  }
}
)"},
        {"a lane load that is used a second time", "sharedload", R"(
module @reject_shared {
  func @sharedload(%p: ptr) -> i32 {
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
    %extra = add i32 %r, %a
    return %extra
  }
}
)"},
        {"a store between the lanes and the root", "storebetween", R"(
module @reject_store {
  func @storebetween(%p: ptr, %q: ptr) -> i32 {
  entry:
    %p0 = ptr.offset ptr %p, 0
    %p1 = ptr.offset ptr %p, 4
    %p2 = ptr.offset ptr %p, 8
    %p3 = ptr.offset ptr %p, 12
    %a = load i32 %p0 align 4
    %b = load i32 %p1 align 4
    %c = load i32 %p2 align 4
    %zero = const i32 0
    store i32 %zero, %q align 4
    %d = load i32 %p3 align 4
    %ab = add i32 %a, %b
    %cd = add i32 %c, %d
    %r = add i32 %ab, %cd
    return %r
  }
}
)"},
        {"a call between the lanes and the root", "callbetween", R"(
module @reject_call {
  extern func @sink(%a: i32) -> i32
  func @callbetween(%p: ptr) -> i32 {
  entry:
    %p0 = ptr.offset ptr %p, 0
    %p1 = ptr.offset ptr %p, 4
    %p2 = ptr.offset ptr %p, 8
    %p3 = ptr.offset ptr %p, 12
    %a = load i32 %p0 align 4
    %b = load i32 %p1 align 4
    %c = load i32 %p2 align 4
    %d = load i32 %p3 align 4
    %ignored = call i32 @sink(%a)
    %ab = add i32 %a, %b
    %cd = add i32 %c, %d
    %r = add i32 %ab, %cd
    return %r
  }
}
)"},
        {"only three lanes, below one vector and not a power of two", "three", R"(
module @reject_three {
  func @three(%p: ptr) -> i32 {
  entry:
    %p0 = ptr.offset ptr %p, 0
    %p1 = ptr.offset ptr %p, 4
    %p2 = ptr.offset ptr %p, 8
    %a = load i32 %p0 align 4
    %b = load i32 %p1 align 4
    %c = load i32 %p2 align 4
    %ab = add i32 %a, %b
    %r = add i32 %ab, %c
    return %r
  }
}
)"},
    };

    for (const auto& entry : cases) {
        forge::machine::LowerResult lowered;
        const auto* function = lower_one(entry.source, entry.name, lowered, entry.why);
        if (function == nullptr) continue;
        const bool packed = find_opcode(*function, Opcode::reduce_add_i32_contiguous) != nullptr ||
                            find_opcode(*function, Opcode::reduce_add_i64_contiguous) != nullptr;
        check(!packed, std::string("AArch64 refuses to form a reduction with ") + entry.why);
    }
}

} // namespace

int main() {
    test_floating_opcode_matrix();
    test_floating_expression_slp();
    test_reduction_is_formed_and_wired_correctly();
    test_reduction_shapes_that_must_be_recognized();
    test_reduction_shapes_that_must_be_rejected();
    test_i32_reduction();
    test_i64_reduction_uses_two_accumulators();
    test_vector_chain();
    test_chain_half_vector_tail();
    test_postfix_dag();
    test_reusable_dag_materializes_shared_node_once();
    test_packed_spill_reload();
    test_frame_access_uses_a_single_instruction();
    if (failures != 0) {
        std::cerr << failures << " AArch64 packed codegen checks failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "AArch64 packed codegen tests passed\n";
    return EXIT_SUCCESS;
}
