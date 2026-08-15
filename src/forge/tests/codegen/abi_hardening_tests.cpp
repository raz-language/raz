// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/codegen/x86_64/encoder.hpp"
#include "forge/ir/parser.hpp"
#include "forge/ir/verifier.hpp"
#include "forge/machine/lower.hpp"
#include "forge/jit/engine.hpp"
#include "forge/machine/verifier.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string_view>

namespace {
struct NativePair {
    std::uint64_t first;
    std::uint64_t second;
};

struct NativeMixed {
    std::uint64_t whole;
    double fraction;
};

extern "C" std::uint64_t forge_test_host_sum_pair(NativePair pair) {
    return pair.first + pair.second;
}

extern "C" NativePair forge_test_host_make_pair(std::uint64_t first, std::uint64_t second) {
    return NativePair{first, second};
}

extern "C" NativeMixed forge_test_host_make_mixed(std::uint64_t whole, double fraction) {
    return NativeMixed{whole, fraction};
}

#if defined(_WIN32)
constexpr auto host_abi = forge::codegen::x86_64::Abi::windows;
#else
constexpr auto host_abi = forge::codegen::x86_64::Abi::system_v;
#endif

void require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "failure: " << message << '\n';
        std::exit(1);
    }
}

const forge::codegen::x86_64::EncodedFunction& find_function(
    const forge::codegen::x86_64::ImageEncodeResult& encoded,
    std::string_view name) {
    for (const auto& function : encoded.functions)
        if (function.name == name) return function;
    std::cerr << "failure: missing encoded function " << name << '\n';
    std::exit(1);
}
}

int main() {
    const auto parsed = forge::ir::parse_module(R"(
module @abi_hardening {
  func @abi_stress(%i0: i64, %a: f64, %i1: i64, %b: f64, %c: f64, %d: f64) -> f64 {
  entry:
    %result = call f64 @mixed_weighted(%i1, %d, %i0, %c, %b, %a)
    return %result
  }
  func @fifth_argument(%a0: i64, %a1: i64, %a2: i64, %a3: i64, %a4: i64) -> i64 {
  entry:
    return %a4
  }
  func @produce_next_argument(%value: i64) -> i64 {
  entry:
    %one = const i64 1
    %result = add i64 %value %one
    return %result
  }
  func @select_third_of_five(%a0: i64, %a1: i64, %a2: i64, %a3: i64, %a4: i64) -> i64 {
  entry:
    return %a2
  }
  func @forwarded_result_with_stack_argument(%value: i64, %tail: i64) -> i64 {
  entry:
    %a0 = const i64 10
    %a1 = const i64 20
    %a3 = const i64 40
    %produced = call i64 @produce_next_argument(%value)
    %result = call i64 @select_third_of_five(%a0, %a1, %produced, %a3, %tail)
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
}
)");
    require(parsed.ok(), "ABI fixture did not parse");
    require(forge::ir::verify_module(*parsed.module).empty(), "ABI fixture failed IR verification");
    const auto lowered = forge::machine::lower_module(*parsed.module);
    require(lowered.ok(), "ABI fixture failed machine lowering");

    const auto sysv = forge::codegen::x86_64::encode_image(
        *lowered.module, forge::codegen::x86_64::Abi::system_v);
    require(sysv.ok(), "System V ABI encoding failed");
    const auto& sysv_stress = find_function(sysv, "abi_stress");
    require(sysv_stress.abi_register_argument_snapshot_count == 0,
            "System V call retained floating register snapshots after parallel-copy lowering");
    require(sysv_stress.abi_stack_argument_count == 0,
            "System V unexpectedly stack-passed the six mixed arguments");
    require(sysv_stress.abi_shadow_space_byte_count == 0,
            "System V emitted Windows shadow space");
    require(sysv_stress.abi_mixed_class_call_count == 1,
            "System V mixed-class call was not counted");

    const auto windows = forge::codegen::x86_64::encode_image(
        *lowered.module, forge::codegen::x86_64::Abi::windows);
    require(windows.ok(), "Windows x64 ABI encoding failed");
    const auto& windows_stress = find_function(windows, "abi_stress");
    require(windows_stress.abi_register_argument_snapshot_count == 0,
            "Windows x64 retained floating register snapshots after parallel-copy lowering");
    require(windows_stress.abi_stack_argument_count == 2,
            "Windows x64 did not place arguments five and six on the stack");
    require(windows_stress.abi_shadow_space_byte_count == 32,
            "Windows x64 did not reserve exactly 32 bytes of shadow space");
    require(windows_stress.abi_mixed_class_call_count == 1,
            "Windows x64 mixed-class call was not counted");
    require((windows_stress.abi_alignment_padding_byte_count & 7U) == 0U,
            "Windows x64 call padding is not slot aligned");
    const auto& windows_fifth_argument = find_function(windows, "fifth_argument");
    require(windows_fifth_argument.leaf_frame_elided_count == 0,
            "Windows x64 frameless function cannot safely address the fifth incoming argument");

#if defined(_WIN32) || (defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__)))
    // Exercise Windows x64 machine code even when this test runs on a SysV host.
    // GCC/Clang's ms_abi function-pointer attribute supplies the correct entry
    // convention, while all calls inside the fixture are Forge-to-Forge.  The
    // regression specifically keeps one call result in RAX for argument #3 of a
    // five-argument call while argument #5 must be staged on the stack.
    auto windows_loaded = forge::jit::load(*lowered.module, forge::codegen::x86_64::Abi::windows);
    require(windows_loaded.ok(), "Windows x64 forwarding fixture failed JIT loading");
#if defined(_WIN32)
    using WindowsForwardedCall = std::uint64_t (*)(std::uint64_t, std::uint64_t);
#else
    using WindowsForwardedCall = std::uint64_t (__attribute__((ms_abi)) *)(std::uint64_t, std::uint64_t);
#endif
    const auto windows_forwarded_call = reinterpret_cast<WindowsForwardedCall>(
        windows_loaded.engine->lookup("forwarded_result_with_stack_argument"));
    require(windows_forwarded_call != nullptr, "missing Windows x64 forwarding regression entry");
    require(windows_forwarded_call(41, 99) == 42,
            "Windows x64 stack-argument staging clobbered a forwarded RAX call result");
#endif

    const auto aggregate_parsed = forge::ir::parse_module(R"(
module @aggregate_abi_interop {
  struct @Pair { first: i64, second: i64 }
  struct @Mixed { whole: i64, fraction: f64 }
  extern c func @forge_test_host_sum_pair(%pair: owned struct @Pair) -> i64
  extern c func @forge_test_host_make_pair(%first: i64, %second: i64) -> owned struct @Pair
  extern c func @forge_test_host_make_mixed(%whole: i64, %fraction: f64) -> owned struct @Mixed

  c func @forge_sum_pair(%pair: owned struct @Pair) -> i64 {
  entry:
    %first_ptr = struct.field.name.address ptr %pair @Pair first
    %second_ptr = struct.field.name.address ptr %pair @Pair second
    %first = load i64 %first_ptr align 8
    %second = load i64 %second_ptr align 8
    %sum = add i64 %first %second
    return %sum
  }

  c func @forge_make_pair(%first: i64, %second: i64) -> owned struct @Pair {
  entry:
    %pair = stack.alloc.struct ptr @Pair
    %first_ptr = struct.field.name.address ptr %pair @Pair first
    %second_ptr = struct.field.name.address ptr %pair @Pair second
    store i64 %first %first_ptr align 8
    store i64 %second %second_ptr align 8
    return %pair
  }

  c func @forge_make_mixed(%whole: i64, %fraction: f64) -> owned struct @Mixed {
  entry:
    %value = stack.alloc.struct ptr @Mixed
    %whole_ptr = struct.field.name.address ptr %value @Mixed whole
    %fraction_ptr = struct.field.name.address ptr %value @Mixed fraction
    store i64 %whole %whole_ptr align 8
    store f64 %fraction %fraction_ptr align 8
    return %value
  }

  func @forge_calls_host_make_mixed_fraction(%whole: i64, %fraction: f64) -> f64 {
  entry:
    %value = call ptr @forge_test_host_make_mixed(%whole, %fraction)
    %fraction_ptr = struct.field.name.address ptr %value @Mixed fraction
    %result = load f64 %fraction_ptr align 8
    return %result
  }

  func @forge_calls_host_make_pair(%first: i64, %second: i64) -> i64 {
  entry:
    %pair = call ptr @forge_test_host_make_pair(%first, %second)
    %first_ptr = struct.field.name.address ptr %pair @Pair first
    %second_ptr = struct.field.name.address ptr %pair @Pair second
    %left = load i64 %first_ptr align 8
    %right = load i64 %second_ptr align 8
    %sum = add i64 %left %right
    return %sum
  }

  func @forge_calls_host(%first: i64, %second: i64) -> i64 {
  entry:
    %pair = stack.alloc.struct ptr @Pair
    %first_ptr = struct.field.name.address ptr %pair @Pair first
    %second_ptr = struct.field.name.address ptr %pair @Pair second
    store i64 %first %first_ptr align 8
    store i64 %second %second_ptr align 8
    %sum = call i64 @forge_test_host_sum_pair(%pair)
    return %sum
  }
}
)");
    require(aggregate_parsed.ok(), "aggregate ABI interop fixture did not parse");
    require(forge::ir::verify_module(*aggregate_parsed.module).empty(),
            "aggregate ABI interop fixture failed IR verification");
    const auto aggregate_lowered = forge::machine::lower_module(*aggregate_parsed.module);
    if (!aggregate_lowered.ok()) {
        for (const auto& diagnostic : aggregate_lowered.diagnostics) std::cerr << diagnostic.message << '\n';
    }

    require(aggregate_lowered.ok(), "aggregate ABI interop fixture failed machine lowering");
    require(forge::machine::verify_module(*aggregate_lowered.module).empty(),
            "aggregate ABI interop machine module failed verification");
    auto loaded = forge::jit::load(*aggregate_lowered.module, host_abi, [](std::string_view name) -> std::optional<std::uintptr_t> {
        if (name == "forge_test_host_sum_pair")
            return reinterpret_cast<std::uintptr_t>(&forge_test_host_sum_pair);
        if (name == "forge_test_host_make_pair")
            return reinterpret_cast<std::uintptr_t>(&forge_test_host_make_pair);
        if (name == "forge_test_host_make_mixed")
            return reinterpret_cast<std::uintptr_t>(&forge_test_host_make_mixed);
        return std::nullopt;
    });
    require(loaded.ok(), "aggregate ABI interop fixture failed JIT loading");

    using ForgeSumPair = std::uint64_t (*)(NativePair);
    const auto forge_sum_pair = reinterpret_cast<ForgeSumPair>(loaded.engine->lookup("forge_sum_pair"));
    require(forge_sum_pair != nullptr, "missing Forge aggregate-parameter entry");
    const auto native_to_forge_value = forge_sum_pair(NativePair{19, 23});
    require(native_to_forge_value == 42,
            "native C++ to Forge by-value aggregate call failed");

    using ForgeCallsHost = std::uint64_t (*)(std::uint64_t, std::uint64_t);
    const auto forge_calls_host = reinterpret_cast<ForgeCallsHost>(loaded.engine->lookup("forge_calls_host"));
    require(forge_calls_host != nullptr, "missing Forge-to-host aggregate call entry");
    const auto forge_to_native_value = forge_calls_host(20, 22);
    require(forge_to_native_value == 42,
            "Forge to native C++ by-value aggregate call failed");

    using ForgeMakePair = NativePair (*)(std::uint64_t, std::uint64_t);
    const auto forge_make_pair = reinterpret_cast<ForgeMakePair>(loaded.engine->lookup("forge_make_pair"));
    require(forge_make_pair != nullptr, "missing Forge aggregate-return entry");
    const auto native_return = forge_make_pair(17, 25);
    require(native_return.first == 17 && native_return.second == 25,
            "Forge to native C++ by-value aggregate return failed");

    using ForgeCallsHostMakePair = std::uint64_t (*)(std::uint64_t, std::uint64_t);
    const auto forge_calls_host_make_pair = reinterpret_cast<ForgeCallsHostMakePair>(
        loaded.engine->lookup("forge_calls_host_make_pair"));
    require(forge_calls_host_make_pair != nullptr, "missing native aggregate-return call entry");
    require(forge_calls_host_make_pair(18, 24) == 42,
            "native C++ to Forge by-value aggregate return failed");

    using ForgeMakeMixed = NativeMixed (*)(std::uint64_t, double);
    const auto forge_make_mixed = reinterpret_cast<ForgeMakeMixed>(loaded.engine->lookup("forge_make_mixed"));
    require(forge_make_mixed != nullptr, "missing Forge mixed aggregate-return entry");
    const auto mixed_return = forge_make_mixed(42, 3.5);
    require(mixed_return.whole == 42 && mixed_return.fraction == 3.5,
            "Forge mixed INTEGER/SSE aggregate return failed");

    using ForgeCallsHostMakeMixed = double (*)(std::uint64_t, double);
    const auto forge_calls_host_make_mixed = reinterpret_cast<ForgeCallsHostMakeMixed>(
        loaded.engine->lookup("forge_calls_host_make_mixed_fraction"));
    require(forge_calls_host_make_mixed != nullptr, "missing native mixed aggregate-return call entry");
    require(forge_calls_host_make_mixed(7, 9.25) == 9.25,
            "native mixed INTEGER/SSE aggregate return to Forge failed");
    return 0;
}
