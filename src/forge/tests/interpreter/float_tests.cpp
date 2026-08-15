// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string_view>

#include "forge/interpreter/interpreter.hpp"
#include "forge/machine/lower.hpp"
#include "forge/jit/engine.hpp"
#include "forge/ir/parser.hpp"
#include "forge/ir/verifier.hpp"

namespace {
double host_blend(double left, std::uint64_t scale, double right) { return (left + right) * static_cast<double>(scale); }

void require(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

forge::ir::Module parse(std::string_view source) {
    auto result = forge::ir::parse_module(source);
    if (!result.module) {
        for (const auto& diagnostic : result.diagnostics) std::cerr << diagnostic.message << '\n';
    }

    require(result.module.has_value(), "floating module parse failed");
    const auto diagnostics = forge::ir::verify_module(*result.module);
    if (!diagnostics.empty()) for (const auto& diagnostic : diagnostics) std::cerr << diagnostic.message << '\n';
    require(diagnostics.empty(), "floating module verification failed");
    return std::move(*result.module);
}

double run(const forge::ir::Module& module, std::string_view function) {
    auto result = forge::interpreter::execute(module, function);
    if (!result.diagnostics.empty()) for (const auto& diagnostic : result.diagnostics) std::cerr << diagnostic.message << '\n';
    require(result.value.has_value(), "floating execution failed");
    require(result.value->kind() == forge::interpreter::Value::Kind::floating, "expected floating result");
    return result.value->floating_value();
}
}

int main() {
    const auto module = parse(R"(
module @floating_interpreter {
  func @f64_math() -> f64 {
  entry:
    %a = const f64 1.5
    %b = const f64 2.25
    %sum = add f64 %a %b
    %scale = const f64 4.0
    %product = mul f64 %sum %scale
    %divisor = const f64 3.0
    %result = div f64 %product %divisor
    return %result
  }
  func @f32_memory() -> f32 {
  entry:
    %buffer = stack.alloc ptr 4 align 4
    %value = const f32 6.5
    store f32 %value %buffer align 4
    %loaded = load f32 %buffer align 4
    %negative = neg f32 %loaded
    return %negative
  }
  func @scientific() -> f64 {
  entry:
    %value = const f64 1.25e2
    return %value
  }
  func @native_math(%a: f64, %b: f64) -> f64 {
  entry:
    %sum = add f64 %a %b
    %scale = const f64 2.0
    %result = mul f64 %sum %scale
    return %result
  }

  signature @FloatBinary(%a: f64, %b: f64) -> f64
  extern func @host_blend(%left: f64, %scale: i64, %right: f64) -> f64
  func @float_add(%a: f64, %b: f64) -> f64 {
  entry:
    %result = add f64 %a %b
    return %result
  }
  func @direct_float_call(%a: f64, %b: f64) -> f64 {
  entry:
    %result = call f64 @float_add(%a, %b)
    return %result
  }
  func @indirect_float_call(%a: f64, %b: f64) -> f64 {
  entry:
    %target = func.address ptr @float_add as @FloatBinary
    %result = call.indirect f64 %target as @FloatBinary(%a, %b)
    return %result
  }
  func @external_float_call(%left: f64, %scale: i64, %right: f64) -> f64 {
  entry:
    %result = call f64 @host_blend(%left, %scale, %right)
    return %result
  }
  func @mixed_float_call(%left: i64, %a: f64, %right: i64, %b: f64) -> f64 {
  entry:
    %sum = call f64 @float_add(%a, %b)
    %scale = const f64 2.0
    %result = mul f64 %sum %scale
    return %result
  }
  func @sum_nine(%a0: f64, %a1: f64, %a2: f64, %a3: f64, %a4: f64, %a5: f64, %a6: f64, %a7: f64, %a8: f64) -> f64 {
  entry:
    %s01 = add f64 %a0 %a1
    %s23 = add f64 %a2 %a3
    %s45 = add f64 %a4 %a5
    %s67 = add f64 %a6 %a7
    %s03 = add f64 %s01 %s23
    %s47 = add f64 %s45 %s67
    %s07 = add f64 %s03 %s47
    %result = add f64 %s07 %a8
    return %result
  }
  func @stack_float_call() -> f64 {
  entry:
    %a0 = const f64 1.0
    %a1 = const f64 2.0
    %a2 = const f64 3.0
    %a3 = const f64 4.0
    %a4 = const f64 5.0
    %a5 = const f64 6.0
    %a6 = const f64 7.0
    %a7 = const f64 8.0
    %a8 = const f64 9.0
    %result = call f64 @sum_nine(%a0, %a1, %a2, %a3, %a4, %a5, %a6, %a7, %a8)
    return %result
  }
  func @mixed_weighted(%i0: i64, %a: f64, %i1: i64, %b: f64, %c: f64, %d: f64) -> f64 {
  entry:
    %ten = const f64 10.0
    %hundred = const f64 100.0
    %thousand = const f64 1000.0
    %wb = mul f64 %b %ten
    %wc = mul f64 %c %hundred
    %wd = mul f64 %d %thousand
    %ab = add f64 %a %wb
    %abc = add f64 %ab %wc
    %result = add f64 %abc %wd
    return %result
  }
  func @mixed_reordered_call(%i0: i64, %a: f64, %i1: i64, %b: f64, %c: f64, %d: f64) -> f64 {
  entry:
    %result = call f64 @mixed_weighted(%i1, %d, %i0, %c, %b, %a)
    return %result
  }
  func @comparison() -> i1 {
  entry:
    %a = const f64 3.5
    %b = const f64 4.5
    %result = cmp.lt f64 %a %b
    return %result
  }
  func @floating_edge_swap(%flag: i1) -> f64 {
  entry:
    %left = const f64 1.0
    %right = const f64 2.0
    branch %flag, merge(%left, %right), merge(%right, %left)
  merge(%first: f64, %second: f64):
    %result = sub f64 %first %second
    return %result
  }
}
)");
    require(std::abs(run(module, "f64_math") - 5.0) < 1e-12, "f64 arithmetic mismatch");
    require(std::abs(run(module, "f32_memory") + 6.5) < 1e-6, "f32 memory mismatch");
    require(std::abs(run(module, "scientific") - 125.0) < 1e-12, "scientific literal mismatch");
    auto comparison = forge::interpreter::execute(module, "comparison");
    require(comparison.value.has_value() && comparison.value->kind() == forge::interpreter::Value::Kind::integer && comparison.value->bits() == 1,
            "floating comparison mismatch");

    auto lowered = forge::machine::lower_module(module);
    if (!lowered.diagnostics.empty()) for (const auto& diagnostic : lowered.diagnostics) std::cerr << diagnostic.message << '\n';
    require(lowered.module.has_value(), "floating native lowering failed");
#if defined(_WIN32)
    constexpr auto abi = forge::codegen::x86_64::Abi::windows;
#else
    constexpr auto abi = forge::codegen::x86_64::Abi::system_v;
#endif
    auto loaded = forge::jit::load(*lowered.module, abi, [](std::string_view name) -> std::optional<std::uintptr_t> {
        if (name == "host_blend") return reinterpret_cast<std::uintptr_t>(&host_blend);
        return std::nullopt;
    });
    if (!loaded.diagnostics.empty()) for (const auto& diagnostic : loaded.diagnostics) std::cerr << diagnostic.message << '\n';
    require(loaded.ok(), "floating JIT load failed");
    auto* native_math = loaded.engine->lookup("native_math");
    require(native_math != nullptr, "floating JIT entry missing");
    const auto native_result = reinterpret_cast<double (*)(double, double)>(native_math)(1.5, 2.25);
    require(std::abs(native_result - 7.5) < 1e-12, "native f64 arithmetic mismatch");
    auto* native_memory = loaded.engine->lookup("f32_memory");
    require(native_memory != nullptr, "native f32 memory entry missing");
    const auto native_f32 = reinterpret_cast<float (*)()>(native_memory)();
    require(std::abs(native_f32 + 6.5F) < 1e-6F, "native f32 memory mismatch");

    auto* direct_call = loaded.engine->lookup("direct_float_call");
    require(direct_call != nullptr, "direct floating call entry missing");
    require(std::abs(reinterpret_cast<double (*)(double, double)>(direct_call)(1.25, 2.75) - 4.0) < 1e-12,
            "direct floating call mismatch");
    auto* indirect_call = loaded.engine->lookup("indirect_float_call");
    require(indirect_call != nullptr, "indirect floating call entry missing");
    require(std::abs(reinterpret_cast<double (*)(double, double)>(indirect_call)(3.5, 4.5) - 8.0) < 1e-12,
            "indirect floating call mismatch");
    auto* external_call = loaded.engine->lookup("external_float_call");
    require(external_call != nullptr, "external floating call entry missing");
    require(std::abs(reinterpret_cast<double (*)(double, std::uint64_t, double)>(external_call)(1.5, 3, 2.5) - 12.0) < 1e-12,
            "external mixed floating call mismatch");
    auto* mixed_call = loaded.engine->lookup("mixed_float_call");
    require(mixed_call != nullptr, "mixed floating call entry missing");
    require(std::abs(reinterpret_cast<double (*)(std::uint64_t, double, std::uint64_t, double)>(mixed_call)(7, 1.5, 9, 2.5) - 8.0) < 1e-12,
            "mixed integer/floating ABI mismatch");
    auto* stack_call = loaded.engine->lookup("stack_float_call");
    require(stack_call != nullptr, "stack floating call entry missing");
    require(std::abs(reinterpret_cast<double (*)()>(stack_call)() - 45.0) < 1e-12,
            "stack-passed floating call mismatch");
    auto* mixed_reordered_call = loaded.engine->lookup("mixed_reordered_call");
    require(mixed_reordered_call != nullptr, "mixed reordered ABI entry missing");
    require(std::abs(reinterpret_cast<double (*)(std::uint64_t, double, std::uint64_t, double, double, double)>(mixed_reordered_call)(7, 1.0, 9, 2.0, 3.0, 4.0) - 1234.0) < 1e-12,
            "mixed reordered register arguments were clobbered during ABI marshaling");
    auto* native_comparison = loaded.engine->lookup("comparison");
    require(native_comparison != nullptr, "native floating comparison entry missing");
    require(reinterpret_cast<std::uint32_t (*)()>(native_comparison)() == 1U, "native floating comparison mismatch");
    auto* floating_edge_swap = loaded.engine->lookup("floating_edge_swap");
    require(floating_edge_swap != nullptr, "floating edge-copy entry missing");
    require(std::abs(reinterpret_cast<double (*)(std::uint32_t)>(floating_edge_swap)(1U) + 1.0) < 1e-12,
            "floating true-edge parallel copy mismatch");
    require(std::abs(reinterpret_cast<double (*)(std::uint32_t)>(floating_edge_swap)(0U) - 1.0) < 1e-12,
            "floating false-edge parallel copy mismatch");
    return 0;
}
