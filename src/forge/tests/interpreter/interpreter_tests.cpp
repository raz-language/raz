// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

#include "forge/interpreter/interpreter.hpp"
#include "forge/ir/parser.hpp"
#include "forge/ir/verifier.hpp"
#include "forge/pass/pass.hpp"
#include "forge/transforms/scalar.hpp"

namespace {
void require(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

forge::ir::Module parse(std::string_view source) {
    auto result = forge::ir::parse_module(source);
    require(result.module.has_value(), "parse failed");
    require(forge::ir::verify_module(*result.module).empty(), "verification failed");
    return std::move(*result.module);
}

std::int64_t run(const forge::ir::Module& module, std::string_view function,
                 std::vector<forge::interpreter::Value> arguments = {}) {
    auto result = forge::interpreter::execute(module, function, arguments);
    if (!result.diagnostics.empty()) {
        for (const auto& diagnostic : result.diagnostics) std::cerr << diagnostic.message << '\n';
    }

    require(result.value.has_value(), "execution failed");
    require(result.value->kind() == forge::interpreter::Value::Kind::integer, "expected integer result");
    return result.value->signed_value();
}
}

int main() {
    const auto module = parse(R"(
module @interpreter {
  global @counter: i64 = 40
  constant @two: i64 = 2
  func @read_global() -> i64 {
  entry:
    %address = global.address ptr @counter
    %value = load i64 %address
    return %value
  }
  func @update_global() -> i64 {
  entry:
    %counter_address = global.address ptr @counter
    %two_address = global.address ptr @two
    %counter = load i64 %counter_address
    %two_value = load i64 %two_address
    %updated = add i64 %counter %two_value
    store i64 %updated %counter_address
    %result = load i64 %counter_address
    return %result
  }
  func @write_constant() -> void {
  entry:
    %address = global.address ptr @two
    %value = const i64 9
    store i64 %value %address
    return
  }
  func @factorial(%value: i64) -> i64 {
  entry:
    %one = const i64 1
    %base = cmp.le i64 %value %one
    branch %base, done(%one), recurse(%value)
  recurse(%current: i64):
    %next = sub i64 %current %one
    %partial = call i64 @factorial(%next)
    %product = mul i64 %current %partial
    return %product
  done(%answer: i64):
    return %answer
  }
  func @memory(%left: i64, %right: i64) -> i64 {
  entry:
    %buffer = stack.alloc ptr 16
    %second = ptr.offset ptr %buffer 8
    store i64 %left %buffer
    store i64 %right %second
    %a = load i64 %buffer
    %b = load i64 %second
    %sum = add i64 %a %b
    return %sum
  }
  func @aligned_memory(%value: i64) -> i64 {
  entry:
    %buffer = stack.alloc ptr 16 align 16
    store i64 %value %buffer align 8
    %result = load i64 %buffer align 8
    return %result
  }
  func @misaligned_memory(%value: i64) -> i64 {
  entry:
    %buffer = stack.alloc ptr 16 align 16
    %misaligned = ptr.offset ptr %buffer 1
    store i64 %value %misaligned align 8
    return %value
  }
  func @increment(%value: i64) -> i64 {
  entry:
    %one = const i64 1
    %result = add i64 %value %one
    return %result
  }
  func @indirect(%value: i64) -> i64 {
  entry:
    %target = func.address ptr @increment
    %result = call.indirect i64 %target as @increment(%value)
    return %result
  }
  func @casts(%value: i64) -> i8 {
  entry:
    %small = truncate i8 %value
    return %small
  }
  func @numeric_casts(%value: i64) -> i64 {
  entry:
    %wide = int_to_float.signed f64 %value
    %single = float_truncate f32 %wide
    %again = float_extend f64 %single
    %result = float_to_int.signed i64 %again
    return %result
  }
}
)");
    using forge::interpreter::Value;
    using forge::ir::Type;
    using forge::ir::TypeKind;
    require(run(module, "read_global") == 40, "read global");
    require(run(module, "update_global") == 42, "update global");
    auto constant_write = forge::interpreter::execute(module, "write_constant");
    require(!constant_write.diagnostics.empty(), "constant global must reject stores");
    require(run(module, "factorial", {Value::integer(Type(TypeKind::i64), 10)}) == 3628800, "i64 recursion");
    require(run(module, "memory", {Value::integer(Type(TypeKind::i64), 20), Value::integer(Type(TypeKind::i64), 22)}) == 42, "i64 memory");
    require(run(module, "indirect", {Value::integer(Type(TypeKind::i64), 41)}) == 42, "indirect call");
    require(run(module, "aligned_memory", {Value::integer(Type(TypeKind::i64), 42)}) == 42, "aligned memory");
    const std::vector<Value> misaligned_arguments{Value::integer(Type(TypeKind::i64), 42)};
    auto misaligned = forge::interpreter::execute(module, "misaligned_memory", misaligned_arguments);
    require(!misaligned.diagnostics.empty(), "misaligned store should fail");
    require(run(module, "casts", {Value::integer(Type(TypeKind::i64), 257)}) == 1, "truncate");
    require(run(module, "numeric_casts", {Value::integer(Type(TypeKind::i64), 42)}) == 42, "numeric conversions");


    auto optimized = module;
    forge::pass::PassManager pipeline;
    pipeline.add<forge::transforms::ConstantFoldPass>()
            .add<forge::transforms::CopyPropagationPass>()
            .add<forge::transforms::BranchFoldPass>()
            .add<forge::transforms::SimplifyCFGPass>()
            .add<forge::transforms::DeadCodeEliminationPass>();
    const auto optimization = pipeline.run(optimized);
    (void)optimization;
    require(run(optimized, "factorial", {Value::integer(Type(TypeKind::i64), 10)}) == 3628800, "optimized interpreter equivalence");

    const std::vector<Value> limited_arguments{Value::integer(Type(TypeKind::i64), 10)};
    auto limited = forge::interpreter::execute(module, "factorial", limited_arguments, {}, {.max_steps = 3, .max_call_depth = 1024});
    require(!limited.diagnostics.empty(), "step limit should fail");
    std::cout << "interpreter tests passed\n";
    return 0;
}
