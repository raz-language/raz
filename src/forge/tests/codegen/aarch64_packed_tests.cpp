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
constexpr std::uint32_t neon_binary_mask = 0xFFE0FC00U;
constexpr std::uint32_t neon_add_2d = 0x4EE08400U, neon_add_4s = 0x4EA08400U;
constexpr std::uint32_t neon_sub_4s = 0x6EA08400U, neon_eor_16b = 0x6E201C00U;
constexpr std::uint32_t neon_mov_16b = 0x4EA01C00U;

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

void test_chain_scalar_tail() {
    // Two i32 lanes are eight bytes, below one vector, so the whole pack is the
    // scalar tail. It must still be encoded rather than rejected.
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
          "a sub-vector chain emits no 128-bit loads");
    // Two lanes, each loading four sources and storing one result.
    check(count_masked(stream, 0xFFC00000U, 0xB9400000U) >= 8U,
          "the scalar chain tail loads every source lane with a 32-bit load");
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
    // Retaining the shared value forces a copy before the destructive operation.
    check(count_masked(stream, neon_binary_mask, neon_mov_16b) >= 1U,
          "a retained DAG node is copied aside before being consumed destructively");
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
    test_reduction_is_formed_and_wired_correctly();
    test_reduction_shapes_that_must_be_recognized();
    test_reduction_shapes_that_must_be_rejected();
    test_i32_reduction();
    test_i64_reduction_uses_two_accumulators();
    test_vector_chain();
    test_chain_scalar_tail();
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
