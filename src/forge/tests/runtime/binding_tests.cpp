// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string_view>

#include "forge/ir/binary.hpp"
#include "forge/ir/parser.hpp"
#include "forge/ir/printer.hpp"
#include "forge/ir/verifier.hpp"
#include "forge/jit/engine.hpp"
#include "forge/jit/invoke.hpp"
#include "forge/machine/lower.hpp"
#include "forge/machine/verifier.hpp"
#include "forge/runtime/bindings.hpp"

namespace {
std::uint64_t host_add(std::uint64_t a, std::uint64_t b) { return a + b; }
std::uint64_t host_increment(std::uint64_t value) { return value + 1; }
std::uint64_t host_bump(std::uint64_t* value) { return ++*value; }
std::uint64_t* host_identity(std::uint64_t* value) { return value; }
void host_make_pair(void* result, std::uint64_t first, std::uint64_t second) {
    const std::array<std::uint64_t, 2> pair{first, second};
    std::memcpy(result, pair.data(), sizeof(pair));
}

void host_mutate_pair(void* pair, std::uint64_t replacement) {
    std::memcpy(pair, &replacement, sizeof(replacement));
}

void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
}

int main() {
    try {
        forge::runtime::BindingRegistry registry;
        forge::runtime::Binding add;
        add.signature.parameters = {forge::ir::Type(forge::ir::TypeKind::i64), forge::ir::Type(forge::ir::TypeKind::i64)};
        add.signature.result = forge::ir::Type(forge::ir::TypeKind::i64);
        add.native_address = reinterpret_cast<std::uintptr_t>(&host_add);
        add.interpreter = [](std::span<const forge::interpreter::Value> args, forge::Diagnostics&) -> std::optional<forge::interpreter::Value> {
            return forge::interpreter::Value::integer(forge::ir::Type(forge::ir::TypeKind::i64), args[0].bits() + args[1].bits());
        };
        require(registry.bind("host_add", std::move(add)).empty(), "valid binding was rejected");
        require(registry.contains("host_add"), "binding lookup failed");
        require(registry.resolve_native("host_add").has_value(), "native resolver failed");
        require(registry.interpreter_map().contains("host_add"), "interpreter map failed");

        forge::runtime::Binding bump;
        bump.signature.parameters = {forge::ir::Type(forge::ir::TypeKind::ptr)};
        bump.signature.result = forge::ir::Type(forge::ir::TypeKind::i64);
        bump.native_address = reinterpret_cast<std::uintptr_t>(&host_bump);
        bump.interpreter = [](std::span<const forge::interpreter::Value> args, forge::Diagnostics& diagnostics) -> std::optional<forge::interpreter::Value> {
            if (args.size() != 1 || args[0].kind() != forge::interpreter::Value::Kind::pointer || args[0].host_address() == nullptr ||
                args[0].as_pointer().object->size() - args[0].as_pointer().offset < sizeof(std::uint64_t)) {
                diagnostics.push_back({forge::DiagnosticSeverity::error, "host_bump requires an eight-byte host pointer", {}});
                return std::nullopt;
            }
            auto* value = static_cast<std::uint64_t*>(args[0].host_address());
            ++*value;
            return forge::interpreter::Value::integer(forge::ir::Type(forge::ir::TypeKind::i64), *value);
        };
        require(registry.bind("host_bump", std::move(bump)).empty(), "pointer binding was rejected");

        forge::runtime::Binding identity;
        identity.signature.parameters = {forge::ir::Type(forge::ir::TypeKind::ptr)};
        identity.signature.result = forge::ir::Type(forge::ir::TypeKind::ptr);
        identity.native_address = reinterpret_cast<std::uintptr_t>(&host_identity);
        identity.interpreter = [](std::span<const forge::interpreter::Value> args, forge::Diagnostics&) -> std::optional<forge::interpreter::Value> {
            return args[0];
        };
        require(registry.bind("host_identity", std::move(identity)).empty(), "pointer-return binding was rejected");

        forge::runtime::Binding make_pair;
        make_pair.signature.parameters = {forge::ir::Type(forge::ir::TypeKind::i64), forge::ir::Type(forge::ir::TypeKind::i64)};
        make_pair.signature.result = forge::ir::Type(forge::ir::TypeKind::ptr);
        make_pair.signature.result_aggregate_kind = forge::ir::AggregateRefKind::structure;
        make_pair.signature.result_aggregate_name = "Pair";
        make_pair.signature.result_owned = true;
        make_pair.native_address = reinterpret_cast<std::uintptr_t>(&host_make_pair);
        make_pair.interpreter = [](std::span<const forge::interpreter::Value> args, forge::Diagnostics&) -> std::optional<forge::interpreter::Value> {
            auto object = std::make_shared<forge::interpreter::MemoryObject>();
            object->bytes.resize(16);
            const auto first = args[0].bits();
            const auto second = args[1].bits();
            std::memcpy(object->bytes.data(), &first, sizeof(first));
            std::memcpy(object->bytes.data() + sizeof(first), &second, sizeof(second));
            return forge::interpreter::Value::pointer({std::move(object), 0});
        };
        require(registry.bind("host_make_pair", std::move(make_pair)).empty(), "owned aggregate return binding was rejected");

        forge::runtime::Binding mutate_pair;
        mutate_pair.signature.parameters = {forge::ir::Type(forge::ir::TypeKind::ptr), forge::ir::Type(forge::ir::TypeKind::i64)};
        mutate_pair.signature.parameter_aggregate_kinds = {forge::ir::AggregateRefKind::structure, forge::ir::AggregateRefKind::scalar};
        mutate_pair.signature.parameter_aggregate_names = {"Pair", ""};
        mutate_pair.signature.parameter_owned = {true, false};
        mutate_pair.signature.result = forge::ir::Type(forge::ir::TypeKind::void_);
        mutate_pair.native_address = reinterpret_cast<std::uintptr_t>(&host_mutate_pair);
        mutate_pair.interpreter = [](std::span<const forge::interpreter::Value> args, forge::Diagnostics& diagnostics) -> std::optional<forge::interpreter::Value> {
            if (args.size() != 2 || args[0].kind() != forge::interpreter::Value::Kind::pointer || args[0].remaining_bytes() < sizeof(std::uint64_t)) {
                diagnostics.push_back({forge::DiagnosticSeverity::error, "host_mutate_pair requires an owned Pair", {}});
                return std::nullopt;
            }
            const auto replacement = args[1].bits();
            std::memcpy(args[0].as_pointer().object->data() + args[0].as_pointer().offset, &replacement, sizeof(replacement));
            return forge::interpreter::Value::void_value();
        };
        require(registry.bind("host_mutate_pair", std::move(mutate_pair)).empty(), "owned aggregate parameter binding was rejected");

        forge::runtime::Binding inspect_pair;
        inspect_pair.signature.parameters = {forge::ir::Type(forge::ir::TypeKind::ptr)};
        inspect_pair.signature.parameter_aggregate_kinds = {forge::ir::AggregateRefKind::structure};
        inspect_pair.signature.parameter_aggregate_names = {"Pair"};
        inspect_pair.signature.parameter_borrow_modes = {forge::ir::BorrowMode::immutable};
        inspect_pair.signature.result = forge::ir::Type(forge::ir::TypeKind::i64);
        inspect_pair.native_address = reinterpret_cast<std::uintptr_t>(&host_identity);
        inspect_pair.interpreter = [](std::span<const forge::interpreter::Value> args, forge::Diagnostics&) -> std::optional<forge::interpreter::Value> {
            return forge::interpreter::Value::integer(forge::ir::Type(forge::ir::TypeKind::i64), args[0].bits());
        };
        require(registry.bind("host_inspect_pair", std::move(inspect_pair)).empty(), "borrowed aggregate parameter binding was rejected");

        const auto borrowed_binding_module = forge::ir::parse_module(R"(
module @borrowed_binding {
struct @Pair { first: i64, second: i64 }
extern func @host_inspect_pair(%pair: borrow struct @Pair) -> i64
}
)");
        require(borrowed_binding_module.ok(), "borrowed binding module did not parse");
        require(forge::ir::verify_module(*borrowed_binding_module.module).empty(), "borrowed binding module did not verify");
        require(registry.validate(*borrowed_binding_module.module).empty(), "borrowed binding metadata did not validate");

        alignas(8) std::uint64_t shared_counter = 40;
        forge::runtime::GlobalBinding counter_binding;
        counter_binding.type = forge::ir::Type(forge::ir::TypeKind::i64);
        counter_binding.address = &shared_counter;
        counter_binding.size = sizeof(shared_counter);
        counter_binding.alignment = alignof(std::uint64_t);
        require(registry.bind_global("shared_counter", counter_binding).empty(), "external global binding was rejected");
        require(registry.contains_global("shared_counter"), "external global lookup failed");
        require(registry.native_global_resolver()("shared_counter").has_value(), "native global resolver failed");
        require(registry.interpreter_globals().contains("shared_counter"), "interpreter global map failed");

        alignas(8) const std::uint64_t shared_limit = 99;
        forge::runtime::GlobalBinding limit_binding;
        limit_binding.type = forge::ir::Type(forge::ir::TypeKind::i64);
        limit_binding.address = const_cast<std::uint64_t*>(&shared_limit);
        limit_binding.size = sizeof(shared_limit);
        limit_binding.alignment = alignof(std::uint64_t);
        limit_binding.read_only = true;
        require(registry.bind_global("shared_limit", limit_binding).empty(), "external constant binding was rejected");

        alignas(16) std::uint8_t shared_bytes[16]{};
        forge::runtime::GlobalBinding bytes_binding;
        bytes_binding.type = forge::ir::Type(forge::ir::TypeKind::i8);
        bytes_binding.address = shared_bytes;
        bytes_binding.size = sizeof(shared_bytes);
        bytes_binding.alignment = 16;
        require(registry.bind_global("shared_bytes", bytes_binding).empty(), "external byte-buffer binding was rejected");

        forge::runtime::Binding duplicate;
        duplicate.native_address = reinterpret_cast<std::uintptr_t>(&host_add);
        require(!registry.bind("host_add", std::move(duplicate)).empty(), "duplicate binding was accepted");

        const auto parsed = forge::ir::parse_module(R"(
module @bindings {
  extern func @host_add(%a: i64, %b: i64) -> i64
  extern func @missing(%value: i32) -> i32
}
)");
        require(parsed.module.has_value(), "binding test module did not parse");
        const auto diagnostics = registry.validate(*parsed.module);
        require(diagnostics.size() == 1, "missing binding validation did not report exactly one error");

        forge::runtime::BindingRegistry mismatch;
        forge::runtime::Binding wrong;
        wrong.signature.parameters = {forge::ir::Type(forge::ir::TypeKind::i32)};
        wrong.signature.result = forge::ir::Type(forge::ir::TypeKind::i64);
        wrong.native_address = reinterpret_cast<std::uintptr_t>(&host_add);
        require(mismatch.bind("host_add", std::move(wrong)).empty(), "mismatch fixture binding failed");
        const auto mismatch_diagnostics = mismatch.validate(*parsed.module);
        require(mismatch_diagnostics.size() >= 2, "signature mismatch validation was incomplete");


        const auto executable = forge::ir::parse_module(R"(
module @runtime_integration {
extern func @host_add(%a: i64, %b: i64) -> i64
func @entry(%a: i64, %b: i64) -> i64 {
entry:
  %value = call i64 @host_add(%a, %b)
  return %value
}
}
)");
        require(executable.ok(), "runtime integration module did not parse");
        require(forge::ir::verify_module(*executable.module).empty(), "runtime integration module did not verify");
        require(registry.validate(*executable.module).empty(), "runtime integration bindings did not validate");
        const forge::interpreter::Value interpreter_args[] = {
            forge::interpreter::Value::integer(forge::ir::Type(forge::ir::TypeKind::i64), 19),
            forge::interpreter::Value::integer(forge::ir::Type(forge::ir::TypeKind::i64), 23)};
        const auto interpreter_result = forge::interpreter::execute(
            *executable.module, "entry", interpreter_args, registry.interpreter_map());
        require(interpreter_result.diagnostics.empty() && interpreter_result.value.has_value(), "bound interpreter call failed");
        require(interpreter_result.value->bits() == 42, "bound interpreter call returned wrong value");


        const auto pointer_module = forge::ir::parse_module(R"(
module @pointer_runtime {
extern func @host_bump(%address: ptr) -> i64
func @bump_entry(%address: ptr) -> i64 {
entry:
  %value = call i64 @host_bump(%address)
  return %value
}
}
)");
        require(pointer_module.ok(), "pointer runtime module did not parse");
        require(forge::ir::verify_module(*pointer_module.module).empty(), "pointer runtime module did not verify");
        require(registry.validate(*pointer_module.module).empty(), "pointer runtime bindings did not validate");
        std::uint64_t interpreter_storage = 40;
        const forge::interpreter::Value pointer_args[] = {
            forge::interpreter::Value::host_pointer(&interpreter_storage, sizeof(interpreter_storage))};
        const auto pointer_interpreter_result = forge::interpreter::execute(
            *pointer_module.module, "bump_entry", pointer_args, registry.interpreter_map());
        require(pointer_interpreter_result.diagnostics.empty() && pointer_interpreter_result.value.has_value(), "bound interpreter pointer call failed");
        require(pointer_interpreter_result.value->bits() == 41 && interpreter_storage == 41, "interpreter pointer mutation was not visible");

        const auto pointer_return_module = forge::ir::parse_module(R"(
module @pointer_return_runtime {
extern func @host_identity(%address: ptr) -> ptr
func @identity_entry(%address: ptr) -> ptr {
entry:
  %value = call ptr @host_identity(%address)
  return %value
}
}
)");
        require(pointer_return_module.ok(), "pointer-return runtime module did not parse");
        require(forge::ir::verify_module(*pointer_return_module.module).empty(), "pointer-return runtime module did not verify");
        require(registry.validate(*pointer_return_module.module).empty(), "pointer-return runtime bindings did not validate");
        std::uint64_t pointer_return_storage = 88;
        const forge::interpreter::Value pointer_return_args[] = {
            forge::interpreter::Value::host_pointer(&pointer_return_storage, sizeof(pointer_return_storage))};
        require(pointer_return_args[0].remaining_bytes() == sizeof(pointer_return_storage), "host pointer extent is incorrect");
        require(pointer_return_args[0].is_aligned(alignof(std::uint64_t)), "host pointer alignment was not detected");
        const auto pointer_return_interpreter = forge::interpreter::execute(
            *pointer_return_module.module, "identity_entry", pointer_return_args, registry.interpreter_map());
        require(pointer_return_interpreter.diagnostics.empty() && pointer_return_interpreter.value.has_value(), "bound interpreter pointer return failed");
        require(pointer_return_interpreter.value->host_address() == &pointer_return_storage, "interpreter pointer return changed the address");


        const auto owned_external_module = forge::ir::parse_module(R"(
module @external_owned_return {
struct @Pair { first: i64, second: i64 }
extern func @host_make_pair(%first: i64, %second: i64) -> owned struct @Pair
func @sum_host_pair(%first: i64, %second: i64) -> i64 {
entry:
  %pair = call ptr @host_make_pair(%first, %second)
  %first_address = struct.field.name.address ptr %pair @Pair first
  %second_address = struct.field.name.address ptr %pair @Pair second
  %loaded_first = load i64 %first_address align 8
  %loaded_second = load i64 %second_address align 8
  %sum = add i64 %loaded_first %loaded_second
  return %sum
}
}
)");
        require(owned_external_module.ok(), "owned external return module did not parse");
        require(forge::ir::verify_module(*owned_external_module.module).empty(), "owned external return module did not verify");
        require(registry.validate(*owned_external_module.module).empty(), "owned external return binding did not validate");
        const forge::interpreter::Value owned_external_args[] = {
            forge::interpreter::Value::integer(forge::ir::Type(forge::ir::TypeKind::i64), 19),
            forge::interpreter::Value::integer(forge::ir::Type(forge::ir::TypeKind::i64), 23)};
        const auto owned_external_interpreter = forge::interpreter::execute(
            *owned_external_module.module, "sum_host_pair", owned_external_args, registry.interpreter_map());
        require(owned_external_interpreter.diagnostics.empty() && owned_external_interpreter.value.has_value() &&
            owned_external_interpreter.value->bits() == 42, "owned external interpreter return failed");

        const auto global_module = forge::ir::parse_module(R"(
module @external_globals {
extern global @shared_counter: i64
extern constant @shared_limit: i64
func @increment_shared() -> i64 {
entry:
  %counter = global.address ptr @shared_counter
  %limit = global.address ptr @shared_limit
  %current = load i64 %counter align 8
  %one = const i64 1
  %next = add i64 %current %one
  store i64 %next %counter align 8
  %unused = load i64 %limit align 8
  return %next
}
}
)");
        require(global_module.ok(), "external-global module did not parse");
        require(forge::ir::verify_module(*global_module.module).empty(), "external-global module did not verify");
        require(registry.validate(*global_module.module).empty(), "external-global bindings did not validate");
        const auto printed_globals = forge::ir::print_module(*global_module.module);
        require(printed_globals.find("extern global @shared_counter") != std::string::npos, "external global did not print");
        const auto global_interpreter = forge::interpreter::execute(
            *global_module.module, "increment_shared", {}, registry.interpreter_map(), registry.interpreter_globals());
        require(global_interpreter.diagnostics.empty() && global_interpreter.value.has_value(), "external-global interpreter execution failed");
        require(global_interpreter.value->bits() == 41 && shared_counter == 41, "external-global interpreter mutation failed");


        const auto byte_global_module = forge::ir::parse_module(R"(
module @external_bytes {
extern global @shared_bytes: i8[16] align 16
func @write_bytes() -> i64 {
entry:
  %address = global.address ptr @shared_bytes
  %value = const i64 72623859790382856
  store i64 %value %address align 8
  %loaded = load i64 %address align 8
  return %loaded
}
}
)");
        require(byte_global_module.ok(), "external byte-buffer module did not parse");
        require(forge::ir::verify_module(*byte_global_module.module).empty(), "external byte-buffer module did not verify");
        require(registry.validate(*byte_global_module.module).empty(), "external byte-buffer binding did not validate");
        const auto byte_global_interpreter = forge::interpreter::execute(
            *byte_global_module.module, "write_bytes", {}, registry.interpreter_map(), registry.interpreter_globals());
        require(byte_global_interpreter.diagnostics.empty() && byte_global_interpreter.value.has_value(), "external byte-buffer interpreter execution failed");
        require(byte_global_interpreter.value->bits() == 72623859790382856ULL, "external byte-buffer interpreter result was wrong");

        const auto owned_parameter_module = forge::ir::parse_module(R"(
module @external_owned_parameter {
struct @Pair { first: i64, second: i64 }
global @original: struct @Pair = { first: 19, second: 23 }
extern func @host_mutate_pair(%pair: owned struct @Pair, %replacement: i64) -> void
func @preserves_original(%replacement: i64) -> i64 {
entry:
  %pair = global.address ptr @original
  call void @host_mutate_pair(%pair, %replacement)
  %first_address = struct.field.name.address ptr %pair @Pair first
  %first = load i64 %first_address align 8
  return %first
}
}
)");
        require(owned_parameter_module.ok(), "external owned parameter module did not parse");
        require(forge::ir::verify_module(*owned_parameter_module.module).empty(), "external owned parameter module did not verify");
        require(registry.validate(*owned_parameter_module.module).empty(), "external owned parameter binding did not validate");
        const forge::interpreter::Value owned_parameter_args[] = {
            forge::interpreter::Value::integer(forge::ir::Type(forge::ir::TypeKind::i64), 999)};
        const auto owned_parameter_interpreter = forge::interpreter::execute(
            *owned_parameter_module.module, "preserves_original", owned_parameter_args, registry.interpreter_map());
        require(owned_parameter_interpreter.diagnostics.empty() && owned_parameter_interpreter.value.has_value() &&
            owned_parameter_interpreter.value->bits() == 19, "external owned interpreter parameter aliased caller storage");

        forge::runtime::Binding callback;
        callback.signature.parameters = {forge::ir::Type(forge::ir::TypeKind::i64)};
        callback.signature.result = forge::ir::Type(forge::ir::TypeKind::i64);
        callback.native_address = reinterpret_cast<std::uintptr_t>(&host_increment);
        callback.interpreter = [](std::span<const forge::interpreter::Value> args, forge::Diagnostics&) -> std::optional<forge::interpreter::Value> {
            return forge::interpreter::Value::integer(forge::ir::Type(forge::ir::TypeKind::i64), args[0].bits() + 1);
        };
        require(registry.bind_callback("host_increment", "Unary", std::move(callback)).empty(), "callback binding failed");

        const auto callback_module = forge::ir::parse_module(R"(
module @callback_test {
  signature @Unary(%value: i64) -> i64

  func @dispatch(%value: i64) -> i64 {
  entry:
    %target = callback.address ptr @host_increment as @Unary
    %result = call.indirect i64 %target as @Unary(%value)
    return %result
  }
}
)");
        require(callback_module.ok(), "callback module parse failed");
        require(forge::ir::verify_module(*callback_module.module).empty(), "callback module verification failed");
        require(registry.validate(*callback_module.module).empty(), "callback registry validation failed");
        const std::array callback_args{forge::interpreter::Value::integer(forge::ir::Type(forge::ir::TypeKind::i64), 41)};
        const auto callback_interpreted = forge::interpreter::execute(
            *callback_module.module, "dispatch", callback_args, registry.interpreter_map());
        require(callback_interpreted.diagnostics.empty() && callback_interpreted.value && callback_interpreted.value->bits() == 42,
            "runtime-created interpreter callback failed");


        const auto callback_parameter_module = forge::ir::parse_module(R"(
module @callback_parameter_test {
  signature @Unary(%value: i64) -> i64
  signature @Binary(%left: i64, %right: i64) -> i64

  func @apply(%callback: callback @Unary, %value: i64) -> i64 {
  entry:
    %result = call.indirect i64 %callback as @Unary(%value)
    return %result
  }

  func @dispatch(%value: i64) -> i64 {
  entry:
    %target = callback.address ptr @host_increment as @Unary
    %result = call i64 @apply(%target, %value)
    return %result
  }
}
)");
        require(callback_parameter_module.ok(), "callback parameter module parse failed");
        require(forge::ir::verify_module(*callback_parameter_module.module).empty(), "callback parameter module verification failed");
        const auto callback_parameter_text = forge::ir::print_module(*callback_parameter_module.module);
        require(callback_parameter_text.find("%callback: callback @Unary") != std::string::npos,
            "callback parameter did not print canonically");
        const auto callback_parameter_binary = forge::ir::write_binary(*callback_parameter_module.module);
        require(callback_parameter_binary.ok(), "callback parameter binary write failed");
        const auto callback_parameter_roundtrip = forge::ir::read_binary(callback_parameter_binary.bytes);
        require(callback_parameter_roundtrip.ok() &&
            callback_parameter_roundtrip.module.functions()[2].parameters[0].function_signature_name == "Unary",
            "callback parameter signature did not survive binary IR round-trip");
        const std::array callback_parameter_args{
            forge::interpreter::Value::function("host_increment"),
            forge::interpreter::Value::integer(forge::ir::Type(forge::ir::TypeKind::i64), 41)};
        const auto callback_parameter_interpreted = forge::interpreter::execute(
            *callback_parameter_module.module, "apply", callback_parameter_args, registry.interpreter_map());
        require(callback_parameter_interpreted.diagnostics.empty() && callback_parameter_interpreted.value &&
            callback_parameter_interpreted.value->bits() == 42, "host callback parameter failed in interpreter");

        const auto mismatched_callback_parameter = forge::ir::parse_module(R"(
module @callback_parameter_mismatch {
  signature @Unary(%value: i64) -> i64
  signature @Binary(%left: i64, %right: i64) -> i64
  func @bad(%callback: callback @Unary, %value: i64) -> i64 {
  entry:
    %result = call.indirect i64 %callback as @Binary(%value, %value)
    return %result
  }
}
)");
        require(mismatched_callback_parameter.ok(), "mismatched callback parameter fixture did not parse");
        require(!forge::ir::verify_module(*mismatched_callback_parameter.module).empty(),
            "verifier accepted incompatible callback parameter signature");

#if defined(__x86_64__) || defined(_M_X64)
        const auto lowered = forge::machine::lower_module(*executable.module);
        require(lowered.ok(), "runtime integration lowering failed");
        require(forge::machine::verify_module(*lowered.module).empty(), "runtime integration machine module failed verification");
#if defined(_WIN32)
        constexpr auto abi = forge::codegen::x86_64::Abi::windows;
#else
        constexpr auto abi = forge::codegen::x86_64::Abi::system_v;
#endif
        auto loaded = forge::jit::load(*lowered.module, abi, registry.native_resolver());
        require(loaded.ok(), "bound JIT load failed");
        const std::uint64_t jit_args[] = {19, 23};
        const auto invoked = forge::jit::invoke_integer(loaded.engine->lookup("entry"), jit_args);
        require(invoked.ok() && invoked.bits == 42, "bound JIT call returned wrong value");

        const auto pointer_lowered = forge::machine::lower_module(*pointer_module.module);
        require(pointer_lowered.ok(), "pointer runtime lowering failed");
        require(forge::machine::verify_module(*pointer_lowered.module).empty(), "pointer runtime machine module failed verification");
        auto pointer_loaded = forge::jit::load(*pointer_lowered.module, abi, registry.native_resolver());
        require(pointer_loaded.ok(), "bound pointer JIT load failed");
        std::uint64_t native_storage = 50;
        const std::uint64_t native_pointer_args[] = {reinterpret_cast<std::uintptr_t>(&native_storage)};
        const auto pointer_invoked = forge::jit::invoke_integer(pointer_loaded.engine->lookup("bump_entry"), native_pointer_args);
        require(pointer_invoked.ok() && pointer_invoked.bits == 51 && native_storage == 51, "bound JIT pointer mutation failed");

        const auto pointer_return_lowered = forge::machine::lower_module(*pointer_return_module.module);
        require(pointer_return_lowered.ok(), "pointer-return runtime lowering failed");
        require(forge::machine::verify_module(*pointer_return_lowered.module).empty(), "pointer-return machine module failed verification");
        auto pointer_return_loaded = forge::jit::load(*pointer_return_lowered.module, abi, registry.native_resolver());
        require(pointer_return_loaded.ok(), "bound pointer-return JIT load failed");
        const std::uint64_t pointer_return_native_args[] = {reinterpret_cast<std::uintptr_t>(&pointer_return_storage)};
        const auto pointer_return_invoked = forge::jit::invoke_pointer(pointer_return_loaded.engine->lookup("identity_entry"), pointer_return_native_args);
        require(pointer_return_invoked.ok() && pointer_return_invoked.pointer() == &pointer_return_storage, "bound JIT pointer return failed");


        const auto owned_external_lowered = forge::machine::lower_module(*owned_external_module.module);
        require(owned_external_lowered.ok(), "owned external return lowering failed");
        require(forge::machine::verify_module(*owned_external_lowered.module).empty(), "owned external return machine verification failed");
        auto owned_external_loaded = forge::jit::load(*owned_external_lowered.module, abi, registry.native_resolver());
        require(owned_external_loaded.ok(), "owned external return JIT load failed");
        const std::uint64_t owned_external_native_args[] = {19, 23};
        const auto owned_external_invoked = forge::jit::invoke_integer(
            owned_external_loaded.engine->lookup("sum_host_pair"), owned_external_native_args);
        require(owned_external_invoked.ok() && owned_external_invoked.bits == 42, "owned external JIT return failed");

        const auto owned_parameter_lowered = forge::machine::lower_module(*owned_parameter_module.module);
        require(owned_parameter_lowered.ok(), "owned external parameter lowering failed");
        require(forge::machine::verify_module(*owned_parameter_lowered.module).empty(), "owned external parameter machine verification failed");
        auto owned_parameter_loaded = forge::jit::load(*owned_parameter_lowered.module, abi, registry.native_resolver());
        require(owned_parameter_loaded.ok(), "owned external parameter JIT load failed");
        const std::uint64_t owned_parameter_native_args[] = {999};
        const auto owned_parameter_invoked = forge::jit::invoke_integer(
            owned_parameter_loaded.engine->lookup("preserves_original"), owned_parameter_native_args);
        require(owned_parameter_invoked.ok() && owned_parameter_invoked.bits == 19, "owned external JIT parameter aliased caller storage");

        const auto global_lowered = forge::machine::lower_module(*global_module.module);
        require(global_lowered.ok(), "external-global lowering failed");
        require(forge::machine::verify_module(*global_lowered.module).empty(), "external-global machine verification failed");
        auto global_loaded = forge::jit::load(*global_lowered.module, abi, registry.native_resolver(), registry.native_global_resolver());
        require(global_loaded.ok(), "external-global JIT load failed");
        const auto global_invoked = forge::jit::invoke_integer(global_loaded.engine->lookup("increment_shared"), {});
        require(global_invoked.ok() && global_invoked.bits == 42 && shared_counter == 42, "external-global JIT mutation failed");

        const auto byte_global_lowered = forge::machine::lower_module(*byte_global_module.module);
        require(byte_global_lowered.ok(), "external byte-buffer lowering failed");
        auto byte_global_loaded = forge::jit::load(*byte_global_lowered.module, abi, registry.native_resolver(), registry.native_global_resolver());
        require(byte_global_loaded.ok(), "external byte-buffer JIT load failed");
        const auto byte_global_invoked = forge::jit::invoke_integer(byte_global_loaded.engine->lookup("write_bytes"), {});
        require(byte_global_invoked.ok() && byte_global_invoked.bits == 72623859790382856ULL, "external byte-buffer JIT mutation failed");


        const auto callback_parameter_lowered = forge::machine::lower_module(*callback_parameter_module.module);
        require(callback_parameter_lowered.ok(), "callback parameter lowering failed");
        require(forge::machine::verify_module(*callback_parameter_lowered.module).empty(), "callback parameter machine verification failed");
        auto callback_parameter_loaded = forge::jit::load(*callback_parameter_lowered.module, abi, registry.native_resolver());
        require(callback_parameter_loaded.ok(), "callback parameter JIT load failed");
        const std::uint64_t callback_parameter_native_args[] = {
            reinterpret_cast<std::uintptr_t>(&host_increment), 41};
        const auto callback_parameter_invoked = forge::jit::invoke_integer(
            callback_parameter_loaded.engine->lookup("apply"), callback_parameter_native_args);
        require(callback_parameter_invoked.ok() && callback_parameter_invoked.bits == 42,
            "host callback parameter failed in JIT");

        const auto callback_lowered = forge::machine::lower_module(*callback_module.module);
        require(callback_lowered.ok(), "callback lowering failed");
        require(forge::machine::verify_module(*callback_lowered.module).empty(), "callback machine verification failed");
        auto callback_loaded = forge::jit::load(*callback_lowered.module, abi, registry.native_resolver());
        require(callback_loaded.ok(), "runtime-created callback JIT load failed");
        const std::uint64_t callback_native_args[] = {41};
        const auto callback_invoked = forge::jit::invoke_integer(callback_loaded.engine->lookup("dispatch"), callback_native_args);
        require(callback_invoked.ok() && callback_invoked.bits == 42, "runtime-created callback JIT invocation failed");

#endif

        std::cout << "Forge runtime binding registry tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "test failure: " << error.what() << '\n';
        return 1;
    }
}
