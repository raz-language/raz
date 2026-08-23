// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "compiler/backend/forge/forge_lowering.hpp"

#include "compiler/diagnostics/diagnostic_engine.hpp"
#include "compiler/semantic/type.hpp"

#include <forge/ir/builder.hpp>
#include <forge/ir/context.hpp>
#include <forge/ir/opcode.hpp>
#include <forge/ir/printer.hpp>
#include <forge/ir/verifier.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace raz::compiler {
namespace {

forge::ir::Type lower_type(const std::string& type, const std::unordered_set<std::string>& enum_types) {
  if (parse_reference_type(type) || parse_raw_pointer_type(type) || parse_function_type(type) || parse_callable_type(type) || parse_dynamic_trait_type(type)) return forge::ir::ptr_type();
  if (type == "void") return forge::ir::void_type();
  if (type == "bool") return forge::ir::i1_type();
  if (type == "i8" || type == "u8" || type == "byte") return forge::ir::i8_type();
  if (type == "i16" || type == "u16") return forge::ir::i16_type();
  if (type == "i32" || type == "u32" || type == "char") return forge::ir::i32_type();
  if (type == "f32") return forge::ir::f32_type();
  if (type == "f64") return forge::ir::f64_type();
  if (enum_types.contains(type)) return forge::ir::i32_type();
  if (!is_builtin_type(type)) return forge::ir::ptr_type();
  return forge::ir::i64_type();
}

forge::ir::Type aggregate_storage_type(std::uint32_t alignment) {
  if (alignment >= 8) return forge::ir::i64_type();
  if (alignment >= 4) return forge::ir::i32_type();
  if (alignment >= 2) return forge::ir::i16_type();
  return forge::ir::i8_type();
}

std::uint64_t forge_type_size(forge::ir::Type type) {
  if (type == forge::ir::i64_type()) return 8;
  if (type == forge::ir::i32_type()) return 4;
  if (type == forge::ir::i16_type()) return 2;
  return 1;
}

std::uint64_t stable_type_id(std::string_view name) {
  std::uint64_t hash = 1469598103934665603ull;
  for (const unsigned char ch : name) { hash ^= ch; hash *= 1099511628211ull; }
  hash &= 0x7fffffffffffffffull;
  return hash == 0 ? 1 : hash;
}

bool is_unsigned_type(const std::string& type) {
  return builtin_type(type).kind == TypeKind::unsigned_integer;
}

forge::ir::Opcode comparison_opcode(MirOpcode opcode, bool is_unsigned) {
  switch (opcode) {
    case MirOpcode::equal: return forge::ir::Opcode::compare_equal;
    case MirOpcode::not_equal: return forge::ir::Opcode::compare_not_equal;
    case MirOpcode::less: return is_unsigned ? forge::ir::Opcode::compare_less_unsigned : forge::ir::Opcode::compare_less_signed;
    case MirOpcode::less_equal: return is_unsigned ? forge::ir::Opcode::compare_less_equal_unsigned : forge::ir::Opcode::compare_less_equal_signed;
    default: return forge::ir::Opcode::compare_equal;
  }
}

}  // namespace

ForgeLowering::ForgeLowering(DiagnosticEngine& diagnostics) : diagnostics_(diagnostics) {}

std::string ForgeLowering::lower_and_print(const MirModule& input) {
  forge::ir::Context context;
  auto& module = context.create_module("raz");
  forge::ir::IRBuilder builder(context, module);

  std::unordered_map<std::string, MirAggregateLayout> aggregate_layouts;
  for (const auto& layout : input.aggregate_layouts) {
    aggregate_layouts.emplace(layout.name, layout);
    const auto element_type = aggregate_storage_type(layout.alignment);
    const auto element_size = forge_type_size(element_type);
    const auto padded_size = std::max<std::uint64_t>(element_size, layout.size);
    const auto element_count = static_cast<std::uint32_t>((padded_size + element_size - 1) / element_size);
    module.arrays().push_back({layout.name, element_type, forge::ir::AggregateRefKind::scalar, {}, element_count, false});
  }

  const std::unordered_set<std::string> enum_types(input.enum_types.begin(), input.enum_types.end());
  std::vector<MirFunction> functions;
  functions.reserve(input.functions.size() * 4);
  for (const auto& function : input.functions) {
    if (!function.is_async || function.is_external) {
      functions.push_back(function);
    } else {
      // The source-level async symbol is a launcher returning its completion future.
      // The original body executes only through the generated poll state machine.
      MirFunction launcher;
      launcher.name = function.name;
      launcher.return_type = "i64";
      launcher.parameters = function.parameters;
      MirBlock entry{"entry", {}};
      std::vector<std::string> constructor_operands{function.async_state_machine.constructor};
      for (const auto& [_, parameter] : function.parameters) constructor_operands.push_back(parameter);
      entry.instructions.push_back({MirOpcode::call, "frame", function.async_state_machine.frame_type + "*mut", std::move(constructor_operands), {}});
      entry.instructions.push_back({MirOpcode::async_frame_future, "future", "i64", {"frame"}, {}});
      entry.instructions.push_back({MirOpcode::call, "initial.poll", "i32", {function.async_state_machine.poll, "frame"}, {}});
      entry.instructions.push_back({MirOpcode::async_frame_destroy, {}, function.async_state_machine.frame_type, {"frame"}, {}});
      entry.instructions.push_back({MirOpcode::return_value, {}, "i64", {"future"}, {}});
      launcher.blocks.push_back(std::move(entry));
      functions.push_back(std::move(launcher));
    }
    for (const auto& generated : function.async_state_machine.generated_functions) {
      MirFunction helper;
      helper.name = generated.name;
      helper.return_type = generated.return_type;
      helper.parameters = generated.parameters;
      helper.blocks = generated.blocks;
      functions.push_back(std::move(helper));
    }
  }

  std::unordered_map<std::string, std::string> return_types;
  std::unordered_map<std::string, std::string> emitted_names;
  return_types.emplace("print", "void");
  for (const auto& function : functions) {
    return_types.emplace(function.name, function.return_type);
    emitted_names.emplace(function.name, function.external_name.empty() ? function.name : function.external_name);
  }

  [[maybe_unused]] const auto async_frame_create_decl = builder.create_function_handle("raz_rt_async_frame_create", forge::ir::ptr_type(), {{"%slot_count", forge::ir::i64_type()}}, true);
  [[maybe_unused]] const auto async_poller_set_decl = builder.create_function_handle("raz_rt_async_frame_set_poller", forge::ir::void_type(), {{"%frame", forge::ir::ptr_type()}, {"%callback", forge::ir::ptr_type()}}, true);
  [[maybe_unused]] const auto async_frame_future_decl = builder.create_function_handle("raz_rt_async_frame_future_i64", forge::ir::i64_type(), {{"%frame", forge::ir::ptr_type()}}, true);
  [[maybe_unused]] const auto async_frame_lifecycle_decl = builder.create_function_handle("raz_rt_async_frame_lifecycle", forge::ir::i32_type(), {{"%frame", forge::ir::ptr_type()}}, true);
  [[maybe_unused]] const auto async_frame_terminal_intent_decl = builder.create_function_handle("raz_rt_async_frame_terminal_intent", forge::ir::i32_type(), {{"%frame", forge::ir::ptr_type()}}, true);
  [[maybe_unused]] const auto async_state_load_decl = builder.create_function_handle("raz_rt_async_state_load", forge::ir::i32_type(), {{"%frame", forge::ir::ptr_type()}}, true);
  [[maybe_unused]] const auto async_state_store_decl = builder.create_function_handle("raz_rt_async_state_store", forge::ir::void_type(), {{"%frame", forge::ir::ptr_type()}, {"%state", forge::ir::i32_type()}}, true);
  [[maybe_unused]] const auto async_slot_load_decl = builder.create_function_handle("raz_rt_async_slot_load", forge::ir::i64_type(), {{"%frame", forge::ir::ptr_type()}, {"%slot", forge::ir::i64_type()}}, true);
  [[maybe_unused]] const auto async_slot_load_ptr_decl = builder.create_function_handle("raz_rt_async_slot_load_ptr", forge::ir::ptr_type(), {{"%frame", forge::ir::ptr_type()}, {"%slot", forge::ir::i64_type()}}, true);
  [[maybe_unused]] const auto async_slot_transfer_projected_decl = builder.create_function_handle("raz_rt_async_slot_transfer_projected", forge::ir::ptr_type(), {{"%frame", forge::ir::ptr_type()}, {"%slot", forge::ir::i64_type()}}, true);
  [[maybe_unused]] const auto async_slot_lifecycle_decl = builder.create_function_handle("raz_rt_async_slot_lifecycle", forge::ir::i32_type(), {{"%frame", forge::ir::ptr_type()}, {"%slot", forge::ir::i64_type()}}, true);
  [[maybe_unused]] const auto async_slot_store_decl = builder.create_function_handle("raz_rt_async_slot_store", forge::ir::void_type(), {{"%frame", forge::ir::ptr_type()}, {"%slot", forge::ir::i64_type()}, {"%value", forge::ir::i64_type()}}, true);
  [[maybe_unused]] const auto async_slot_store_owned_decl = builder.create_function_handle("raz_rt_async_slot_store_owned", forge::ir::void_type(), {{"%frame", forge::ir::ptr_type()}, {"%slot", forge::ir::i64_type()}, {"%value", forge::ir::ptr_type()}, {"%cleanup", forge::ir::ptr_type()}}, true);
  [[maybe_unused]] const auto async_slot_store_owned_bytes_decl = builder.create_function_handle("raz_rt_async_slot_store_owned_bytes", forge::ir::void_type(), {{"%frame", forge::ir::ptr_type()}, {"%slot", forge::ir::i64_type()}, {"%source", forge::ir::ptr_type()}, {"%size", forge::ir::i64_type()}, {"%alignment", forge::ir::i64_type()}, {"%cleanup", forge::ir::ptr_type()}}, true);
  [[maybe_unused]] const auto async_slot_allocate_owned_bytes_decl = builder.create_function_handle("raz_rt_async_slot_allocate_owned_bytes", forge::ir::ptr_type(), {{"%frame", forge::ir::ptr_type()}, {"%slot", forge::ir::i64_type()}, {"%size", forge::ir::i64_type()}, {"%alignment", forge::ir::i64_type()}, {"%cleanup", forge::ir::ptr_type()}}, true);
  [[maybe_unused]] const auto async_slot_allocate_projected_bytes_decl = builder.create_function_handle("raz_rt_async_slot_allocate_projected_bytes", forge::ir::ptr_type(), {{"%frame", forge::ir::ptr_type()}, {"%slot", forge::ir::i64_type()}, {"%size", forge::ir::i64_type()}, {"%alignment", forge::ir::i64_type()}, {"%cleanup", forge::ir::ptr_type()}, {"%mask", forge::ir::i64_type()}, {"%projection_count", forge::ir::i64_type()}}, true);
  [[maybe_unused]] const auto async_slot_projection_set_decl = builder.create_function_handle("raz_rt_async_slot_projection_set", forge::ir::void_type(), {{"%frame", forge::ir::ptr_type()}, {"%slot", forge::ir::i64_type()}, {"%projection", forge::ir::i64_type()}, {"%initialized", forge::ir::i1_type()}}, true);
  [[maybe_unused]] const auto async_slot_projection_range_set_decl = builder.create_function_handle("raz_rt_async_slot_projection_range_set", forge::ir::void_type(), {{"%frame", forge::ir::ptr_type()}, {"%slot", forge::ir::i64_type()}, {"%first", forge::ir::i64_type()}, {"%count", forge::ir::i64_type()}, {"%initialized", forge::ir::i1_type()}}, true);
  [[maybe_unused]] const auto async_slot_projection_batch_set_decl = builder.create_function_handle("raz_rt_async_slot_projection_batch_set", forge::ir::i64_type(), {{"%frame", forge::ir::ptr_type()}, {"%slot", forge::ir::i64_type()}, {"%firsts", forge::ir::ptr_type()}, {"%counts", forge::ir::ptr_type()}, {"%states", forge::ir::ptr_type()}, {"%count", forge::ir::i64_type()}}, true);
  [[maybe_unused]] const auto async_slot_projection_count_decl = builder.create_function_handle("raz_rt_async_slot_projection_count", forge::ir::i64_type(), {{"%frame", forge::ir::ptr_type()}, {"%slot", forge::ir::i64_type()}}, true);
  [[maybe_unused]] const auto async_slot_projection_version_decl = builder.create_function_handle("raz_rt_async_slot_projection_version", forge::ir::i64_type(), {{"%frame", forge::ir::ptr_type()}, {"%slot", forge::ir::i64_type()}}, true);
  [[maybe_unused]] const auto async_slot_projection_mask_decl = builder.create_function_handle("raz_rt_async_slot_projection_mask", forge::ir::i64_type(), {{"%frame", forge::ir::ptr_type()}, {"%slot", forge::ir::i64_type()}}, true);
  [[maybe_unused]] const auto async_slot_take_decl = builder.create_function_handle("raz_rt_async_slot_take", forge::ir::ptr_type(), {{"%frame", forge::ir::ptr_type()}, {"%slot", forge::ir::i64_type()}}, true);
  [[maybe_unused]] const auto async_slot_disarm_decl = builder.create_function_handle("raz_rt_async_slot_disarm", forge::ir::void_type(), {{"%frame", forge::ir::ptr_type()}, {"%slot", forge::ir::i64_type()}}, true);
  [[maybe_unused]] const auto async_slot_address_decl = builder.create_function_handle("raz_rt_async_slot_address", forge::ir::ptr_type(), {{"%frame", forge::ir::ptr_type()}, {"%slot", forge::ir::i64_type()}}, true);
  [[maybe_unused]] const auto async_result_load_decl = builder.create_function_handle("raz_rt_async_result_load", forge::ir::i64_type(), {{"%frame", forge::ir::ptr_type()}}, true);
  [[maybe_unused]] const auto async_result_store_decl = builder.create_function_handle("raz_rt_async_result_store", forge::ir::void_type(), {{"%frame", forge::ir::ptr_type()}, {"%value", forge::ir::i64_type()}}, true);
  [[maybe_unused]] const auto async_cancel_requested_decl = builder.create_function_handle("raz_rt_async_cancel_requested", forge::ir::i1_type(), {{"%frame", forge::ir::ptr_type()}}, true);
  [[maybe_unused]] const auto async_frame_cancel_decl = builder.create_function_handle("raz_rt_async_frame_cancel", forge::ir::void_type(), {{"%frame", forge::ir::ptr_type()}}, true);
  [[maybe_unused]] const auto async_await_poll_decl = builder.create_function_handle("raz_rt_async_await_poll", forge::ir::i32_type(), {{"%frame", forge::ir::ptr_type()}, {"%future", forge::ir::i64_type()}, {"%resume_state", forge::ir::i32_type()}}, true);
  [[maybe_unused]] const auto async_await_result_decl = builder.create_function_handle("raz_rt_async_await_result", forge::ir::i64_type(), {{"%frame", forge::ir::ptr_type()}}, true);
  [[maybe_unused]] const auto async_frame_has_pending_await_decl = builder.create_function_handle("raz_rt_async_frame_has_pending_await", forge::ir::i32_type(), {{"%frame", forge::ir::ptr_type()}}, true);
  [[maybe_unused]] const auto async_frame_await_generation_decl = builder.create_function_handle("raz_rt_async_frame_await_generation", forge::ir::i64_type(), {{"%frame", forge::ir::ptr_type()}}, true);
  [[maybe_unused]] const auto async_frame_destroy_decl = builder.create_function_handle("raz_rt_async_frame_destroy", forge::ir::void_type(), {{"%frame", forge::ir::ptr_type()}}, true);
  [[maybe_unused]] const auto future_continuation_count_decl = builder.create_function_handle("raz_rt_future_continuation_count", forge::ir::i64_type(), {{"%future", forge::ir::ptr_type()}}, true);

  [[maybe_unused]] const auto callable_create_erased_decl = builder.create_function_handle(
      "raz_rt_callable_create_erased", forge::ir::ptr_type(),
      {{"%environment", forge::ir::ptr_type()}, {"%invoke", forge::ir::ptr_type()}, {"%clone", forge::ir::ptr_type()},
       {"%drop", forge::ir::ptr_type()}, {"%kind", forge::ir::i32_type()}, {"%signature", forge::ir::i64_type()},
       {"%argument_size", forge::ir::i64_type()}, {"%result_size", forge::ir::i64_type()}}, true);
  [[maybe_unused]] const auto callable_invoke_erased_decl = builder.create_function_handle(
      "raz_rt_callable_invoke_erased", forge::ir::i32_type(),
      {{"%callable", forge::ir::ptr_type()}, {"%signature", forge::ir::i64_type()}, {"%arguments", forge::ir::ptr_type()},
       {"%argument_size", forge::ir::i64_type()}, {"%result", forge::ir::ptr_type()}, {"%result_size", forge::ir::i64_type()}}, true);
  [[maybe_unused]] const auto callable_clone_erased_decl = builder.create_function_handle(
      "raz_rt_callable_clone_erased", forge::ir::ptr_type(), {{"%callable", forge::ir::ptr_type()}}, true);
  [[maybe_unused]] const auto callable_destroy_erased_decl = builder.create_function_handle(
      "raz_rt_callable_destroy_erased", forge::ir::void_type(), {{"%callable", forge::ir::ptr_type()}}, true);
  [[maybe_unused]] const auto trait_create_erased_decl = builder.create_function_handle(
      "raz_rt_trait_object_create_erased", forge::ir::ptr_type(),
      {{"%data", forge::ir::ptr_type()}, {"%type_id", forge::ir::i64_type()}, {"%trait_id", forge::ir::i64_type()},
       {"%drop", forge::ir::ptr_type()}, {"%methods", forge::ir::ptr_type()}, {"%method_count", forge::ir::i64_type()}}, true);
  [[maybe_unused]] const auto trait_invoke_erased_decl = builder.create_function_handle(
      "raz_rt_trait_object_invoke_erased", forge::ir::i32_type(),
      {{"%object", forge::ir::ptr_type()}, {"%slot", forge::ir::i64_type()}, {"%signature", forge::ir::i64_type()},
       {"%arguments", forge::ir::ptr_type()}, {"%argument_size", forge::ir::i64_type()}, {"%result", forge::ir::ptr_type()},
       {"%result_size", forge::ir::i64_type()}}, true);
  [[maybe_unused]] const auto trait_clone_erased_decl = builder.create_function_handle(
      "raz_rt_trait_object_clone_erased", forge::ir::ptr_type(), {{"%object", forge::ir::ptr_type()}}, true);
  [[maybe_unused]] const auto trait_destroy_erased_decl = builder.create_function_handle(
      "raz_rt_trait_object_destroy_erased", forge::ir::void_type(), {{"%object", forge::ir::ptr_type()}}, true);
  // The public Raz runtime models allocation addresses as usize. Keep the
  // synthesized compiler declaration ABI-compatible with those source-level
  // declarations; compiler-generated closure code bitcasts the address to ptr.
  [[maybe_unused]] const auto dispatch_alloc_decl = builder.create_function_handle(
      "raz_rt_alloc", forge::ir::i64_type(), {{"%size", forge::ir::i64_type()}}, true);
  [[maybe_unused]] const auto dispatch_dealloc_decl = builder.create_function_handle(
      "raz_rt_dealloc", forge::ir::void_type(), {{"%pointer", forge::ir::i64_type()}}, true);
  [[maybe_unused]] const auto dispatch_memcpy_decl = builder.create_function_handle(
      "raz_rt_memcpy", forge::ir::void_type(),
      {{"%destination", forge::ir::ptr_type()}, {"%source", forge::ir::ptr_type()}, {"%size", forge::ir::i64_type()}}, true);

  return_types.emplace("raz_rt_async_frame_create", "void*mut");
  return_types.emplace("raz_rt_async_frame_set_poller", "void");
  return_types.emplace("raz_rt_async_frame_future_i64", "i64");
  return_types.emplace("raz_rt_async_frame_lifecycle", "i32");
  return_types.emplace("raz_rt_async_frame_terminal_intent", "i32");
  return_types.emplace("raz_rt_async_state_load", "i32");
  return_types.emplace("raz_rt_async_state_store", "void");
  return_types.emplace("raz_rt_async_slot_load", "i64");
  return_types.emplace("raz_rt_async_slot_load_ptr", "void*mut");
  return_types.emplace("raz_rt_async_slot_transfer_projected", "void*mut");
  return_types.emplace("raz_rt_async_slot_lifecycle", "i32");
  return_types.emplace("raz_rt_async_slot_store", "void");
  return_types.emplace("raz_rt_async_slot_store_owned", "void");
  return_types.emplace("raz_rt_async_slot_store_owned_bytes", "void");
  return_types.emplace("raz_rt_async_slot_allocate_owned_bytes", "void*mut");
  return_types.emplace("raz_rt_async_slot_allocate_projected_bytes", "void*mut");
  return_types.emplace("raz_rt_async_slot_projection_set", "void");
  return_types.emplace("raz_rt_async_slot_projection_range_set", "void");
  return_types.emplace("raz_rt_async_slot_projection_batch_set", "i64");
  return_types.emplace("raz_rt_async_slot_projection_count", "i64");
  return_types.emplace("raz_rt_async_slot_projection_version", "i64");
  return_types.emplace("raz_rt_async_slot_projection_mask", "i64");
  return_types.emplace("raz_rt_async_slot_take", "void*mut");
  return_types.emplace("raz_rt_async_slot_disarm", "void");
  return_types.emplace("raz_rt_async_slot_address", "void*mut");
  return_types.emplace("raz_rt_async_result_load", "i64");
  return_types.emplace("raz_rt_async_result_store", "void");
  return_types.emplace("raz_rt_async_cancel_requested", "bool");
  return_types.emplace("raz_rt_async_frame_cancel", "void");
  return_types.emplace("raz_rt_async_await_poll", "i32");
  return_types.emplace("raz_rt_async_await_result", "i64");
  return_types.emplace("raz_rt_async_frame_has_pending_await", "i32");
  return_types.emplace("raz_rt_async_frame_await_generation", "i64");
  return_types.emplace("raz_rt_async_frame_destroy", "void");
  return_types.emplace("raz_rt_future_continuation_count", "i64");

  return_types.emplace("raz_rt_callable_create_erased", "void*mut");
  return_types.emplace("raz_rt_callable_invoke_erased", "i32");
  return_types.emplace("raz_rt_callable_clone_erased", "void*mut");
  return_types.emplace("raz_rt_callable_destroy_erased", "void");
  return_types.emplace("raz_rt_trait_object_create_erased", "void*mut");
  return_types.emplace("raz_rt_trait_object_invoke_erased", "i32");
  return_types.emplace("raz_rt_trait_object_clone_erased", "void*mut");
  return_types.emplace("raz_rt_trait_object_destroy_erased", "void");

  std::unordered_map<std::uint64_t, const MirDispatchAbi*> dispatch_abis;
  for (const auto& abi : input.dispatch_abis) dispatch_abis.emplace(abi.signature_id, &abi);
  std::unordered_map<std::string, const MirFunction*> mir_functions;
  for (const auto& function : functions) mir_functions.emplace(function.name, &function);
  std::unordered_map<std::string, std::string> callable_thunks;
  std::unordered_map<std::string, std::string> callable_drop_thunks;
  std::unordered_map<std::string, std::string> trait_thunks;

  for (const auto& function : functions) {
    std::vector<forge::ir::ValueDecl> parameters;
    parameters.reserve(function.parameters.size());
    for (const auto& [type, name] : function.parameters) {
      forge::ir::ValueDecl parameter{"%" + name, lower_type(type, enum_types)};
      if (aggregate_layouts.contains(type)) {
        parameter.aggregate_kind = forge::ir::AggregateRefKind::array;
        parameter.aggregate_name = type;
        parameter.owned = true;
      }
      parameters.push_back(std::move(parameter));
    }
    const auto emitted_name = function.external_name.empty() ? function.name : function.external_name;
    // Some runtime ABI entry points are synthesized above for compiler-generated
    // features and are also declared explicitly by low-level Raz modules. Reuse
    // the existing declaration instead of creating a duplicate Forge symbol.
    const bool already_declared = std::any_of(module.functions().begin(), module.functions().end(),
        [&](const forge::ir::Function& item) { return item.name == emitted_name; });
    if (function.is_external && already_declared) continue;
    const auto forge_function = builder.create_function_handle(emitted_name, lower_type(function.return_type, enum_types), parameters, function.is_external);
    if (aggregate_layouts.contains(function.return_type)) {
      auto& declaration = builder.resolve(forge_function);
      declaration.return_aggregate_kind = forge::ir::AggregateRefKind::array;
      declaration.return_aggregate_name = function.return_type;
      declaration.return_owned = true;
    }
    if (function.is_external) continue;

    std::unordered_map<std::string, forge::ir::BlockHandle> block_handles;
    for (const auto& block : function.blocks) {
      block_handles.emplace(block.name, builder.create_block_handle(forge_function, block.name));
    }

    std::unordered_map<std::string, std::string> values;
    for (const auto& [_, name] : function.parameters) values.emplace(name, "%" + name);

    for (const auto& block : function.blocks) {
      builder.position_at_end(block_handles.at(block.name));
      for (const auto& instruction : block.instructions) {
        const auto operand = [&](std::size_t index) {
          const auto& name = instruction.operands.at(index);
          const auto found = values.find(name);
          return found == values.end() ? name : found->second;
        };
        std::string result;
        switch (instruction.opcode) {
          case MirOpcode::constant:
            result = builder.create_constant(lower_type(instruction.type_name, enum_types), instruction.operands.front());
            break;
          case MirOpcode::copy:
            result = builder.create_copy(lower_type(instruction.type_name, enum_types), operand(0));
            break;
          case MirOpcode::stack_allocate:
            result = builder.create_stack_allocation(
                static_cast<std::uint64_t>(std::stoull(instruction.operands.at(0))),
                static_cast<std::uint32_t>(std::stoul(instruction.operands.at(1))));
            if (aggregate_layouts.contains(instruction.type_name)) {
              auto& current_block = builder.resolve(block_handles.at(block.name));
              auto& allocation = current_block.operations.back();
              allocation.opcode = "stack.alloc.array";
              allocation.operands = {"@" + instruction.type_name};
            }
            break;
          case MirOpcode::load:
            result = builder.create_load(lower_type(instruction.type_name, enum_types), operand(0));
            break;
          case MirOpcode::store:
            builder.create_store(lower_type(instruction.type_name, enum_types), operand(0), operand(1));
            break;
          case MirOpcode::pointer_offset:
            result = builder.create_pointer_offset(operand(0), operand(1));
            break;
          case MirOpcode::drop:
            // Drop is a verified lifetime boundary. User destructors will lower to calls
            // before this marker once the Drop trait is executable.
            break;
          case MirOpcode::storage_live:
          case MirOpcode::storage_dead:
          case MirOpcode::move_value:
          case MirOpcode::borrow_shared:
          case MirOpcode::borrow_exclusive:
          case MirOpcode::borrow_bind:
          case MirOpcode::place_path:
            // Ownership metadata is consumed by MIR verification only.
            break;
          case MirOpcode::add:
            result = builder.create_binary(forge::ir::Opcode::add, lower_type(instruction.type_name, enum_types), operand(0), operand(1));
            break;
          case MirOpcode::subtract:
            result = builder.create_binary(forge::ir::Opcode::subtract, lower_type(instruction.type_name, enum_types), operand(0), operand(1));
            break;
          case MirOpcode::multiply:
            result = builder.create_binary(forge::ir::Opcode::multiply, lower_type(instruction.type_name, enum_types), operand(0), operand(1));
            break;
          case MirOpcode::divide:
            result = builder.create_binary(is_unsigned_type(instruction.type_name) ? forge::ir::Opcode::divide_unsigned : forge::ir::Opcode::divide_signed, lower_type(instruction.type_name, enum_types), operand(0), operand(1));
            break;
          case MirOpcode::remainder:
            result = builder.create_binary(is_unsigned_type(instruction.type_name) ? forge::ir::Opcode::remainder_unsigned : forge::ir::Opcode::remainder_signed, lower_type(instruction.type_name, enum_types), operand(0), operand(1));
            break;
          case MirOpcode::bit_and:
            result = builder.create_binary(forge::ir::Opcode::bit_and, lower_type(instruction.type_name, enum_types), operand(0), operand(1));
            break;
          case MirOpcode::bit_or:
            result = builder.create_binary(forge::ir::Opcode::bit_or, lower_type(instruction.type_name, enum_types), operand(0), operand(1));
            break;
          case MirOpcode::bit_xor:
            result = builder.create_binary(forge::ir::Opcode::bit_xor, lower_type(instruction.type_name, enum_types), operand(0), operand(1));
            break;
          case MirOpcode::shift_left:
            result = builder.create_binary(forge::ir::Opcode::shift_left, lower_type(instruction.type_name, enum_types), operand(0), operand(1));
            break;
          case MirOpcode::shift_right:
            result = builder.create_binary(is_unsigned_type(instruction.type_name) ? forge::ir::Opcode::shift_right_unsigned : forge::ir::Opcode::shift_right_signed, lower_type(instruction.type_name, enum_types), operand(0), operand(1));
            break;
          case MirOpcode::numeric_cast: {
            const auto source_name = instruction.operands.at(1);
            const auto target_name = instruction.type_name;
            const auto source_builtin = builtin_type(source_name);
            const auto target_builtin = builtin_type(target_name);
            const auto source_type = lower_type(source_name, enum_types);
            const auto target_type = lower_type(target_name, enum_types);
            const bool source_pointer = parse_raw_pointer_type(source_name).has_value();
            const bool target_pointer = parse_raw_pointer_type(target_name).has_value();
            const auto pointer_sized_integer = [](std::string_view type) {
              return type == "usize" || type == "isize" || type == "u64" || type == "i64" || type == "uint" || type == "int";
            };
            if ((source_pointer && target_pointer) ||
                (source_pointer && pointer_sized_integer(target_name)) ||
                (target_pointer && pointer_sized_integer(source_name))) {
              if (source_type == target_type) result = builder.create_copy(target_type, operand(0));
              else result = builder.create_cast(forge::ir::Opcode::bitcast, target_type, operand(0));
              break;
            }
            if (source_type == target_type) {
              result = builder.create_copy(target_type, operand(0));
              break;
            }
            const auto integer_like = [](TypeKind kind) {
              return kind == TypeKind::bool_type || kind == TypeKind::signed_integer || kind == TypeKind::unsigned_integer;
            };
            const auto unsigned_like = [](TypeKind kind) {
              // bool is represented as i1 and extends as 0/1.
              return kind == TypeKind::bool_type || kind == TypeKind::unsigned_integer;
            };
            if (integer_like(source_builtin.kind) && integer_like(target_builtin.kind)) {
              const auto width = [](forge::ir::Type type) -> unsigned {
                using forge::ir::TypeKind;
                switch (type.kind()) {
                  case TypeKind::i1: return 1; case TypeKind::i8: return 8; case TypeKind::i16: return 16;
                  case TypeKind::i32: return 32; case TypeKind::i64: return 64; default: return 0;
                }
              };
              if (width(source_type) < width(target_type)) {
                const auto opcode = unsigned_like(source_builtin.kind) ? forge::ir::Opcode::zero_extend : forge::ir::Opcode::sign_extend;
                result = builder.create_cast(opcode, target_type, operand(0));
              } else if (width(source_type) > width(target_type)) {
                result = builder.create_cast(forge::ir::Opcode::truncate, target_type, operand(0));
              } else {
                result = builder.create_copy(target_type, operand(0));
              }
              break;
            }
            if (integer_like(source_builtin.kind) && target_builtin.kind == TypeKind::floating_point) {
              result = builder.create_cast(unsigned_like(source_builtin.kind) ?
                  forge::ir::Opcode::int_to_float_unsigned : forge::ir::Opcode::int_to_float_signed, target_type, operand(0));
              break;
            }
            if (source_builtin.kind == TypeKind::floating_point && integer_like(target_builtin.kind)) {
              result = builder.create_cast(unsigned_like(target_builtin.kind) ?
                  forge::ir::Opcode::float_to_int_unsigned : forge::ir::Opcode::float_to_int_signed, target_type, operand(0));
              break;
            }
            if (source_builtin.kind == TypeKind::floating_point && target_builtin.kind == TypeKind::floating_point) {
              result = builder.create_cast(target_name == "f64" ? forge::ir::Opcode::float_extend : forge::ir::Opcode::float_truncate, target_type, operand(0));
              break;
            }
            diagnostics_.error("D4004", instruction.range, "unsupported numeric cast from '" + source_name + "' to '" + target_name + "'");
            break;
          }
          case MirOpcode::equal:
          case MirOpcode::not_equal:
          case MirOpcode::less:
          case MirOpcode::less_equal:
            result = builder.create_compare(comparison_opcode(instruction.opcode, is_unsigned_type(instruction.type_name)), lower_type(instruction.type_name, enum_types), operand(0), operand(1));
            break;
          case MirOpcode::greater:
            result = builder.create_compare(is_unsigned_type(instruction.type_name) ? forge::ir::Opcode::compare_less_unsigned : forge::ir::Opcode::compare_less_signed, lower_type(instruction.type_name, enum_types), operand(1), operand(0));
            break;
          case MirOpcode::greater_equal:
            result = builder.create_compare(is_unsigned_type(instruction.type_name) ? forge::ir::Opcode::compare_less_equal_unsigned : forge::ir::Opcode::compare_less_equal_signed, lower_type(instruction.type_name, enum_types), operand(1), operand(0));
            break;
          case MirOpcode::async_await:
            diagnostics_.error("D4002", instruction.range, "unexpanded source-level await reached Forge lowering");
            break;
          case MirOpcode::task_spawn:
            // Calls to async source symbols now already return completion futures.
            result = builder.create_copy(lower_type(instruction.type_name, enum_types), operand(0));
            break;
          case MirOpcode::call: {
            std::vector<std::string> arguments;
            const auto target = mir_functions.find(instruction.operands.front());
            for (std::size_t index = 1; index < instruction.operands.size(); ++index) {
              std::string argument = operand(index);
              const auto parameter_index = index - 1;
              if (target != mir_functions.end() && parameter_index < target->second->parameters.size()) {
                const auto& parameter_type = target->second->parameters[parameter_index].first;
                const auto layout = aggregate_layouts.find(parameter_type);
                if (layout != aggregate_layouts.end()) {
                  // Forge tracks named-aggregate identity on SSA pointers. A Raz
                  // field projection is represented in MIR as a raw byte offset,
                  // so passing that projection directly by value loses the
                  // aggregate identity even though its bytes are valid. Materialize
                  // every by-value aggregate argument into a named call-temporary.
                  // This also enforces true by-value isolation instead of allowing
                  // the callee to alias the caller's storage.
                  const auto temporary = builder.create_stack_allocation(layout->second.size, layout->second.alignment);
                  auto& current_block = builder.resolve(block_handles.at(block.name));
                  auto& allocation = current_block.operations.back();
                  allocation.opcode = "stack.alloc.array";
                  allocation.operands = {"@" + parameter_type};
                  allocation.alignment = 0;

                  builder.create_store(forge::ir::ptr_type(), argument, temporary);
                  auto& copy = current_block.operations.back();
                  copy.opcode = "aggregate.copy.array";
                  copy.type = forge::ir::void_type();
                  copy.operands = {temporary, argument, "@" + parameter_type};
                  copy.alignment = 0;
                  argument = temporary;
                }
              }
              arguments.push_back(std::move(argument));
            }
            const auto return_type = return_types.find(instruction.operands.front());
            const auto type = return_type == return_types.end() ? instruction.type_name : return_type->second;
            const auto emitted = emitted_names.find(instruction.operands.front());
            result = builder.create_call(lower_type(type, enum_types), emitted == emitted_names.end() ? instruction.operands.front() : emitted->second, std::move(arguments));
            break;
          }
          case MirOpcode::function_address: {
            const auto emitted = emitted_names.find(instruction.operands.front());
            result = builder.create_function_address(emitted == emitted_names.end() ? instruction.operands.front() : emitted->second);
            break;
          }
          case MirOpcode::call_indirect: {
            std::vector<std::string> arguments;
            for (std::size_t index = 2; index < instruction.operands.size(); ++index) arguments.push_back(operand(index));
            const auto emitted = emitted_names.find(instruction.operands.at(1));
            result = builder.create_indirect_call(lower_type(instruction.type_name, enum_types), operand(0),
                                                  emitted == emitted_names.end() ? instruction.operands.at(1) : emitted->second,
                                                  std::move(arguments));
            break;
          }
          case MirOpcode::callable_create: {
            const auto signature_id = static_cast<std::uint64_t>(std::stoull(instruction.operands.at(2)));
            const auto abi_it = dispatch_abis.find(signature_id);
            const auto callback_it = mir_functions.find(instruction.operands.front());
            if (abi_it == dispatch_abis.end() || callback_it == mir_functions.end() || instruction.operands.size() < 4) {
              diagnostics_.error("D4010", instruction.range, "callable dispatch ABI or callback metadata is unavailable");
              break;
            }
            struct CaptureLayout final {
              std::string type;
              std::uint64_t offset = 0;
              std::uint64_t size = 0;
              std::uint32_t alignment = 1;
              bool inline_storage = false;
              std::size_t value_operand = 0;
              std::string cleanup_symbol;
            };
            constexpr std::size_t metadata_start = 4;
            constexpr std::size_t metadata_width = 7;
            if ((instruction.operands.size() - metadata_start) % metadata_width != 0) {
              diagnostics_.error("D4012", instruction.range, "callable capture layout metadata is malformed");
              break;
            }
            std::vector<CaptureLayout> captures;
            for (std::size_t index = metadata_start; index < instruction.operands.size(); index += metadata_width) {
              CaptureLayout capture;
              capture.type = instruction.operands[index];
              capture.offset = static_cast<std::uint64_t>(std::stoull(instruction.operands[index + 1]));
              capture.size = static_cast<std::uint64_t>(std::stoull(instruction.operands[index + 2]));
              capture.alignment = static_cast<std::uint32_t>(std::stoul(instruction.operands[index + 3]));
              capture.inline_storage = instruction.operands[index + 4] == "inline";
              capture.value_operand = index + 5;
              capture.cleanup_symbol = instruction.operands[index + 6];
              captures.push_back(std::move(capture));
            }
            const auto& abi = *abi_it->second;
            const auto& callback_function = *callback_it->second;
            const std::string thunk_key = instruction.operands.front() + ":" + std::to_string(signature_id);
            auto thunk_it = callable_thunks.find(thunk_key);
            if (thunk_it == callable_thunks.end()) {
              const std::string thunk_name = "__raz_callable_adapter_" + std::to_string(stable_type_id(thunk_key));
              auto thunk_function = builder.create_function_handle(
                  thunk_name, forge::ir::i32_type(),
                  {{"%environment", forge::ir::ptr_type()}, {"%arguments", forge::ir::ptr_type()},
                   {"%argument_size", forge::ir::i64_type()}, {"%result", forge::ir::ptr_type()},
                   {"%result_size", forge::ir::i64_type()}});
              auto thunk_entry = builder.create_block_handle(thunk_function, "entry");
              builder.position_at_end(thunk_entry);
              std::vector<std::string> callback_arguments;
              for (const auto& capture : captures) {
                const auto capture_address = capture.offset == 0 ? std::string{"%environment"} :
                    builder.create_pointer_offset("%environment", std::to_string(capture.offset));
                if (capture.inline_storage) {
                  const auto layout = aggregate_layouts.find(capture.type);
                  if (layout != aggregate_layouts.end()) {
                    // Inline aggregate captures live as untyped bytes inside the callable
                    // environment. Re-materialize them into a named Forge aggregate
                    // temporary before invoking the callback so ABI verification sees
                    // the same aggregate identity as the callback parameter.
                    const auto temporary = builder.create_stack_allocation(layout->second.size, layout->second.alignment);
                    auto& thunk_block = builder.resolve(thunk_entry);
                    auto& allocation = thunk_block.operations.back();
                    allocation.opcode = "stack.alloc.array";
                    allocation.operands = {"@" + capture.type};
                    allocation.alignment = 0;

                    builder.create_store(forge::ir::ptr_type(), capture_address, temporary);
                    auto& copy = thunk_block.operations.back();
                    copy.opcode = "aggregate.copy.array";
                    copy.type = forge::ir::void_type();
                    copy.operands = {temporary, capture_address, "@" + capture.type};
                    copy.alignment = 0;
                    callback_arguments.push_back(temporary);
                  } else {
                    callback_arguments.push_back(capture_address);
                  }
                } else {
                  callback_arguments.push_back(builder.create_load(lower_type(capture.type, enum_types), capture_address, capture.alignment));
                }
              }
              for (const auto& field : abi.arguments) {
                const auto argument_address = field.offset == 0 ? std::string{"%arguments"} :
                    builder.create_pointer_offset("%arguments", std::to_string(field.offset));
                callback_arguments.push_back(builder.create_load(lower_type(field.type_name, enum_types), argument_address, field.alignment));
              }
              const auto emitted = emitted_names.find(instruction.operands.front());
              const auto callback_name = emitted == emitted_names.end() ? instruction.operands.front() : emitted->second;
              const auto callback_result = builder.create_call(lower_type(callback_function.return_type, enum_types), callback_name,
                                                               std::move(callback_arguments));
              if (abi.result_size != 0)
                builder.create_store(lower_type(abi.result_type, enum_types), callback_result, "%result", abi.result_alignment);
              builder.create_return(builder.create_constant(forge::ir::i32_type(), "0"));
              callable_thunks.emplace(thunk_key, thunk_name);
              thunk_it = callable_thunks.find(thunk_key);
              builder.position_at_end(block_handles.at(block.name));
            }
            const auto environment_size = static_cast<std::uint64_t>(std::stoull(instruction.operands.at(3)));
            const auto environment_bits = builder.create_call(forge::ir::i64_type(), "raz_rt_alloc",
                {builder.create_constant(forge::ir::i64_type(), std::to_string(environment_size))});
            const auto environment = builder.create_cast(forge::ir::Opcode::bitcast, forge::ir::ptr_type(), environment_bits);
            bool needs_environment_drop = false;
            for (const auto& capture : captures) {
              const auto capture_address = capture.offset == 0 ? environment :
                  builder.create_pointer_offset(environment, std::to_string(capture.offset));
              if (capture.inline_storage) {
                [[maybe_unused]] const auto copied = builder.create_call(forge::ir::void_type(), "raz_rt_memcpy",
                    {capture_address, operand(capture.value_operand),
                     builder.create_constant(forge::ir::i64_type(), std::to_string(capture.size))});
              } else {
                builder.create_store(lower_type(capture.type, enum_types), operand(capture.value_operand), capture_address, capture.alignment);
              }
              if (capture.cleanup_symbol != "-") needs_environment_drop = true;
            }
            const auto thunk = builder.create_function_address(thunk_it->second);
            const auto null_pointer = builder.create_constant(forge::ir::ptr_type(), "0");
            std::string drop;
            if (!needs_environment_drop) {
              drop = builder.create_function_address("raz_rt_dealloc");
            } else {
              auto drop_it = callable_drop_thunks.find(thunk_key);
              if (drop_it == callable_drop_thunks.end()) {
                const std::string drop_name = "__raz_callable_drop_" + std::to_string(stable_type_id(thunk_key));
                auto drop_function = builder.create_function_handle(drop_name, forge::ir::void_type(), {{"%environment", forge::ir::ptr_type()}});
                auto drop_entry = builder.create_block_handle(drop_function, "entry");
                builder.position_at_end(drop_entry);
                const auto projection_words = builder.create_constant(forge::ir::ptr_type(), "0");
                const auto projection_count = builder.create_constant(forge::ir::i64_type(), "0");
                for (auto capture = captures.rbegin(); capture != captures.rend(); ++capture) {
                  if (capture->cleanup_symbol == "-") continue;
                  const auto capture_address = capture->offset == 0 ? std::string{"%environment"} :
                      builder.create_pointer_offset("%environment", std::to_string(capture->offset));
                  const auto emitted_cleanup = emitted_names.find(capture->cleanup_symbol);
                  const auto cleanup_name = emitted_cleanup == emitted_names.end() ? capture->cleanup_symbol : emitted_cleanup->second;
                  [[maybe_unused]] const auto cleaned = builder.create_call(forge::ir::void_type(), cleanup_name,
                                      {capture_address, projection_words, projection_count});
                }
                const auto drop_environment_bits = builder.create_cast(forge::ir::Opcode::bitcast, forge::ir::i64_type(), "%environment");
                [[maybe_unused]] const auto deallocated = builder.create_call(forge::ir::void_type(), "raz_rt_dealloc", {drop_environment_bits});
                builder.create_return();
                callable_drop_thunks.emplace(thunk_key, drop_name);
                drop_it = callable_drop_thunks.find(thunk_key);
                builder.position_at_end(block_handles.at(block.name));
              }
              drop = builder.create_function_address(drop_it->second);
            }
            const auto kind_value = instruction.operands.at(1) == "FnOnce" ? "2" : (instruction.operands.at(1) == "FnMut" ? "1" : "0");
            result = builder.create_call(forge::ir::ptr_type(), "raz_rt_callable_create_erased",
                {environment, thunk, null_pointer, drop,
                 builder.create_constant(forge::ir::i32_type(), kind_value),
                 builder.create_constant(forge::ir::i64_type(), std::to_string(signature_id)),
                 builder.create_constant(forge::ir::i64_type(), std::to_string(abi.argument_size)),
                 builder.create_constant(forge::ir::i64_type(), std::to_string(abi.result_size))});
            break;
          }
          case MirOpcode::callable_clone:
            result = builder.create_call(forge::ir::ptr_type(), "raz_rt_callable_clone_erased", {operand(0)});
            break;
          case MirOpcode::callable_invoke: {
            const auto signature_id = static_cast<std::uint64_t>(std::stoull(instruction.operands.at(1)));
            const auto abi_it = dispatch_abis.find(signature_id);
            if (abi_it == dispatch_abis.end()) {
              diagnostics_.error("D4011", instruction.range, "callable invocation has no dispatch ABI descriptor");
              break;
            }
            const auto& abi = *abi_it->second;
            const auto null_pointer = builder.create_constant(forge::ir::ptr_type(), "0");
            std::string argument_frame = null_pointer;
            if (abi.argument_size != 0) {
              argument_frame = builder.create_stack_allocation(abi.argument_size, abi.argument_alignment);
              for (std::size_t index = 0; index < abi.arguments.size(); ++index) {
                const auto& field = abi.arguments[index];
                const auto address = field.offset == 0 ? argument_frame :
                    builder.create_pointer_offset(argument_frame, std::to_string(field.offset));
                builder.create_store(lower_type(field.type_name, enum_types), operand(index + 2), address, field.alignment);
              }
            }
            std::string result_frame = null_pointer;
            if (abi.result_size != 0) result_frame = builder.create_stack_allocation(abi.result_size, abi.result_alignment);
            [[maybe_unused]] const auto status = builder.create_call(forge::ir::i32_type(), "raz_rt_callable_invoke_erased",
                {operand(0), builder.create_constant(forge::ir::i64_type(), std::to_string(signature_id)), argument_frame,
                 builder.create_constant(forge::ir::i64_type(), std::to_string(abi.argument_size)), result_frame,
                 builder.create_constant(forge::ir::i64_type(), std::to_string(abi.result_size))});
            if (abi.result_size != 0)
              result = builder.create_load(lower_type(abi.result_type, enum_types), result_frame, abi.result_alignment);
            break;
          }
          case MirOpcode::callable_destroy: {
            [[maybe_unused]] const auto ignored = builder.create_call(forge::ir::void_type(), "raz_rt_callable_destroy_erased", {operand(0)});
            break;
          }
          case MirOpcode::trait_object_create: {
            const auto& trait_name = instruction.operands.at(2);
            const std::size_t method_count = instruction.operands.size() - 3;
            std::vector<const MirDispatchAbi*> trait_abis(method_count, nullptr);
            for (const auto& abi : input.dispatch_abis) {
              if (abi.kind == MirDispatchAbiKind::trait_method && abi.owner == trait_name && abi.vtable_slot < method_count)
                trait_abis[abi.vtable_slot] = &abi;
            }
            const auto method_table = builder.create_stack_allocation(std::max<std::uint64_t>(1, method_count * 32), 8);
            bool valid_table = true;
            for (std::size_t slot = 0; slot < method_count; ++slot) {
              const auto* abi = trait_abis[slot];
              const auto& implementation_name = instruction.operands.at(slot + 3);
              const auto implementation = mir_functions.find(implementation_name);
              if (abi == nullptr || implementation == mir_functions.end()) {
                diagnostics_.error("D4012", instruction.range, "trait object method has no typed dispatch descriptor or implementation");
                valid_table = false;
                continue;
              }
              const std::string thunk_key = implementation_name + ":" + std::to_string(abi->signature_id);
              auto thunk_it = trait_thunks.find(thunk_key);
              if (thunk_it == trait_thunks.end()) {
                const std::string thunk_name = "__raz_trait_adapter_" + std::to_string(stable_type_id(thunk_key));
                auto thunk_function = builder.create_function_handle(
                    thunk_name, forge::ir::i32_type(),
                    {{"%data", forge::ir::ptr_type()}, {"%arguments", forge::ir::ptr_type()},
                     {"%argument_size", forge::ir::i64_type()}, {"%result", forge::ir::ptr_type()},
                     {"%result_size", forge::ir::i64_type()}});
                auto thunk_entry = builder.create_block_handle(thunk_function, "entry");
                builder.position_at_end(thunk_entry);
                std::vector<std::string> method_arguments{"%data"};
                for (const auto& field : abi->arguments) {
                  const auto argument_address = field.offset == 0 ? std::string{"%arguments"} :
                      builder.create_pointer_offset("%arguments", std::to_string(field.offset));
                  method_arguments.push_back(builder.create_load(lower_type(field.type_name, enum_types), argument_address, field.alignment));
                }
                const auto emitted = emitted_names.find(implementation_name);
                const auto target_name = emitted == emitted_names.end() ? implementation_name : emitted->second;
                const auto method_result = builder.create_call(lower_type(implementation->second->return_type, enum_types), target_name,
                                                               std::move(method_arguments));
                if (abi->result_size != 0)
                  builder.create_store(lower_type(abi->result_type, enum_types), method_result, "%result", abi->result_alignment);
                builder.create_return(builder.create_constant(forge::ir::i32_type(), "0"));
                trait_thunks.emplace(thunk_key, thunk_name);
                thunk_it = trait_thunks.find(thunk_key);
                builder.position_at_end(block_handles.at(block.name));
              }
              const auto base = slot == 0 ? method_table : builder.create_pointer_offset(method_table, std::to_string(slot * 32));
              builder.create_store(forge::ir::ptr_type(), builder.create_function_address(thunk_it->second), base, 8);
              builder.create_store(forge::ir::i64_type(), builder.create_constant(forge::ir::i64_type(), std::to_string(abi->signature_id)),
                                   builder.create_pointer_offset(base, "8"), 8);
              builder.create_store(forge::ir::i64_type(), builder.create_constant(forge::ir::i64_type(), std::to_string(abi->argument_size)),
                                   builder.create_pointer_offset(base, "16"), 8);
              builder.create_store(forge::ir::i64_type(), builder.create_constant(forge::ir::i64_type(), std::to_string(abi->result_size)),
                                   builder.create_pointer_offset(base, "24"), 8);
            }
            if (valid_table) {
              const auto null_pointer = builder.create_constant(forge::ir::ptr_type(), "0");
              result = builder.create_call(forge::ir::ptr_type(), "raz_rt_trait_object_create_erased",
                  {operand(0),
                   builder.create_constant(forge::ir::i64_type(), std::to_string(stable_type_id(instruction.operands.at(1)))),
                   builder.create_constant(forge::ir::i64_type(), std::to_string(stable_type_id(trait_name))),
                   null_pointer, method_table,
                   builder.create_constant(forge::ir::i64_type(), std::to_string(method_count))});
            }
            break;
          }
          case MirOpcode::trait_object_clone:
            result = builder.create_call(forge::ir::ptr_type(), "raz_rt_trait_object_clone_erased", {operand(0)});
            break;
          case MirOpcode::trait_object_invoke: {
            const auto signature_id = static_cast<std::uint64_t>(std::stoull(instruction.operands.at(2)));
            const auto abi_it = dispatch_abis.find(signature_id);
            if (abi_it == dispatch_abis.end() || abi_it->second->kind != MirDispatchAbiKind::trait_method) {
              diagnostics_.error("D4013", instruction.range, "trait method invocation has no typed dispatch ABI descriptor");
              break;
            }
            const auto& abi = *abi_it->second;
            const auto null_pointer = builder.create_constant(forge::ir::ptr_type(), "0");
            std::string argument_frame = null_pointer;
            if (abi.argument_size != 0) {
              argument_frame = builder.create_stack_allocation(abi.argument_size, abi.argument_alignment);
              for (std::size_t index = 0; index < abi.arguments.size(); ++index) {
                const auto& field = abi.arguments[index];
                const auto address = field.offset == 0 ? argument_frame :
                    builder.create_pointer_offset(argument_frame, std::to_string(field.offset));
                builder.create_store(lower_type(field.type_name, enum_types), operand(index + 3), address, field.alignment);
              }
            }
            std::string result_frame = null_pointer;
            if (abi.result_size != 0) result_frame = builder.create_stack_allocation(abi.result_size, abi.result_alignment);
            [[maybe_unused]] const auto status = builder.create_call(forge::ir::i32_type(), "raz_rt_trait_object_invoke_erased",
                {operand(0), builder.create_constant(forge::ir::i64_type(), instruction.operands.at(1)),
                 builder.create_constant(forge::ir::i64_type(), std::to_string(signature_id)), argument_frame,
                 builder.create_constant(forge::ir::i64_type(), std::to_string(abi.argument_size)), result_frame,
                 builder.create_constant(forge::ir::i64_type(), std::to_string(abi.result_size))});
            if (abi.result_size != 0)
              result = builder.create_load(lower_type(abi.result_type, enum_types), result_frame, abi.result_alignment);
            break;
          }
          case MirOpcode::trait_object_destroy: {
            [[maybe_unused]] const auto ignored = builder.create_call(
                forge::ir::void_type(), "raz_rt_trait_object_destroy_erased", {operand(0)});
            break;
          }
          case MirOpcode::async_frame_create: {
            const auto slot_count = builder.create_constant(forge::ir::i64_type(), instruction.operands.at(1));
            result = builder.create_call(forge::ir::ptr_type(), "raz_rt_async_frame_create", {slot_count});
            break;
          }
          case MirOpcode::async_poller_set: {
            const auto callback = builder.create_function_address(instruction.operands.at(1));
            [[maybe_unused]] const auto ignored = builder.create_call(forge::ir::void_type(), "raz_rt_async_frame_set_poller", {operand(0), callback});
            break;
          }
          case MirOpcode::async_frame_future:
            result = builder.create_call(forge::ir::i64_type(), "raz_rt_async_frame_future_i64", {operand(0)});
            break;
          case MirOpcode::async_state_load:
            result = builder.create_call(forge::ir::i32_type(), "raz_rt_async_state_load", {operand(0)});
            break;
          case MirOpcode::async_state_store: {
            const auto state = builder.create_constant(forge::ir::i32_type(), instruction.operands.at(1));
            [[maybe_unused]] const auto ignored = builder.create_call(forge::ir::void_type(), "raz_rt_async_state_store", {operand(0), state});
            break;
          }
          case MirOpcode::async_slot_load: {
            const auto slot = builder.create_constant(forge::ir::i64_type(), instruction.operands.at(1));
            const auto result_type = lower_type(instruction.type_name, enum_types);
            result = result_type == forge::ir::ptr_type()
                ? builder.create_call(forge::ir::ptr_type(), "raz_rt_async_slot_load_ptr", {operand(0), slot})
                : builder.create_call(forge::ir::i64_type(), "raz_rt_async_slot_load", {operand(0), slot});
            break;
          }
          case MirOpcode::async_slot_transfer_projected: {
            const auto slot = builder.create_constant(forge::ir::i64_type(), instruction.operands.at(1));
            result = builder.create_call(forge::ir::ptr_type(), "raz_rt_async_slot_transfer_projected", {operand(0), slot});
            break;
          }
          case MirOpcode::async_slot_store: {
            const auto slot = builder.create_constant(forge::ir::i64_type(), instruction.operands.at(1));
            [[maybe_unused]] const auto ignored = builder.create_call(forge::ir::void_type(), "raz_rt_async_slot_store", {operand(0), slot, operand(2)});
            break;
          }
          case MirOpcode::async_slot_store_owned: {
            const auto slot = builder.create_constant(forge::ir::i64_type(), instruction.operands.at(1));
            const auto cleanup = builder.create_function_address(instruction.operands.at(3));
            [[maybe_unused]] const auto ignored = builder.create_call(
                forge::ir::void_type(), "raz_rt_async_slot_store_owned", {operand(0), slot, operand(2), cleanup});
            break;
          }
          case MirOpcode::async_slot_store_owned_bytes: {
            const auto slot = builder.create_constant(forge::ir::i64_type(), instruction.operands.at(1));
            const auto size = builder.create_constant(forge::ir::i64_type(), instruction.operands.at(3));
            const auto alignment = builder.create_constant(forge::ir::i64_type(), instruction.operands.at(4));
            const auto cleanup = builder.create_function_address(instruction.operands.at(5));
            [[maybe_unused]] const auto ignored = builder.create_call(
                forge::ir::void_type(), "raz_rt_async_slot_store_owned_bytes",
                {operand(0), slot, operand(2), size, alignment, cleanup});
            break;
          }
          case MirOpcode::async_slot_allocate_owned_bytes: {
            const auto slot = builder.create_constant(forge::ir::i64_type(), instruction.operands.at(1));
            const auto size = builder.create_constant(forge::ir::i64_type(), instruction.operands.at(2));
            const auto alignment = builder.create_constant(forge::ir::i64_type(), instruction.operands.at(3));
            const auto cleanup = builder.create_function_address(instruction.operands.at(4));
            result = builder.create_call(forge::ir::ptr_type(), "raz_rt_async_slot_allocate_owned_bytes",
                                         {operand(0), slot, size, alignment, cleanup});
            break;
          }
          case MirOpcode::async_slot_allocate_projected_bytes: {
            const auto slot = builder.create_constant(forge::ir::i64_type(), instruction.operands.at(1));
            const auto size = builder.create_constant(forge::ir::i64_type(), instruction.operands.at(2));
            const auto alignment = builder.create_constant(forge::ir::i64_type(), instruction.operands.at(3));
            const auto cleanup = builder.create_function_address(instruction.operands.at(4));
            const auto mask = builder.create_constant(forge::ir::i64_type(), instruction.operands.at(5));
            const auto projection_count = builder.create_constant(forge::ir::i64_type(), instruction.operands.at(6));
            result = builder.create_call(forge::ir::ptr_type(), "raz_rt_async_slot_allocate_projected_bytes",
                                         {operand(0), slot, size, alignment, cleanup, mask, projection_count});
            break;
          }
          case MirOpcode::async_slot_projection_set: {
            const auto slot = builder.create_constant(forge::ir::i64_type(), instruction.operands.at(1));
            const auto projection = builder.create_constant(forge::ir::i64_type(), instruction.operands.at(2));
            [[maybe_unused]] const auto ignored = builder.create_call(forge::ir::void_type(), "raz_rt_async_slot_projection_set",
                                                                       {operand(0), slot, projection, operand(3)});
            break;
          }
          case MirOpcode::async_slot_projection_range_set: {
            const auto slot = builder.create_constant(forge::ir::i64_type(), instruction.operands.at(1));
            const auto first = builder.create_constant(forge::ir::i64_type(), instruction.operands.at(2));
            const auto count = builder.create_constant(forge::ir::i64_type(), instruction.operands.at(3));
            [[maybe_unused]] const auto ignored = builder.create_call(forge::ir::void_type(), "raz_rt_async_slot_projection_range_set",
                                                                       {operand(0), slot, first, count, operand(4)});
            break;
          }
          case MirOpcode::async_slot_projection_batch_set: {
            const auto slot = builder.create_constant(forge::ir::i64_type(), instruction.operands.at(1));
            const auto transition_count = static_cast<std::uint64_t>(std::stoull(instruction.operands.at(2)));
            const auto firsts = builder.create_stack_allocation(std::max<std::uint64_t>(1, transition_count * 8), 8);
            const auto counts = builder.create_stack_allocation(std::max<std::uint64_t>(1, transition_count * 8), 8);
            const auto states = builder.create_stack_allocation(std::max<std::uint64_t>(1, transition_count), 1);
            for (std::uint64_t index = 0; index < transition_count; ++index) {
              const auto first_offset = builder.create_constant(forge::ir::i64_type(), std::to_string(index * 8));
              const auto state_offset = builder.create_constant(forge::ir::i64_type(), std::to_string(index));
              const auto first_address = builder.create_pointer_offset(firsts, first_offset);
              const auto count_address = builder.create_pointer_offset(counts, first_offset);
              const auto state_address = builder.create_pointer_offset(states, state_offset);
              builder.create_store(forge::ir::i64_type(),
                                   builder.create_constant(forge::ir::i64_type(), instruction.operands.at(3 + index * 3)),
                                   first_address);
              builder.create_store(forge::ir::i64_type(),
                                   builder.create_constant(forge::ir::i64_type(), instruction.operands.at(4 + index * 3)),
                                   count_address);
              builder.create_store(forge::ir::i1_type(), operand(5 + index * 3), state_address);
            }
            const auto count = builder.create_constant(forge::ir::i64_type(), std::to_string(transition_count));
            result = builder.create_call(forge::ir::i64_type(), "raz_rt_async_slot_projection_batch_set",
                                         {operand(0), slot, firsts, counts, states, count});
            break;
          }
          case MirOpcode::async_slot_projection_mask: {
            const auto slot = builder.create_constant(forge::ir::i64_type(), instruction.operands.at(1));
            result = builder.create_call(forge::ir::i64_type(), "raz_rt_async_slot_projection_mask", {operand(0), slot});
            break;
          }
          case MirOpcode::async_slot_take: {
            const auto slot = builder.create_constant(forge::ir::i64_type(), instruction.operands.at(1));
            result = builder.create_call(forge::ir::ptr_type(), "raz_rt_async_slot_take", {operand(0), slot});
            break;
          }
          case MirOpcode::async_slot_disarm: {
            const auto slot = builder.create_constant(forge::ir::i64_type(), instruction.operands.at(1));
            [[maybe_unused]] const auto ignored = builder.create_call(forge::ir::void_type(), "raz_rt_async_slot_disarm", {operand(0), slot});
            break;
          }
          case MirOpcode::async_slot_address: {
            const auto slot = builder.create_constant(forge::ir::i64_type(), instruction.operands.at(1));
            result = builder.create_call(forge::ir::ptr_type(), "raz_rt_async_slot_address", {operand(0), slot});
            break;
          }
          case MirOpcode::async_result_load:
            result = builder.create_call(forge::ir::i64_type(), "raz_rt_async_result_load", {operand(0)});
            break;
          case MirOpcode::async_result_store: {
            [[maybe_unused]] const auto ignored = builder.create_call(forge::ir::void_type(), "raz_rt_async_result_store", {operand(0), operand(1)});
            break;
          }
          case MirOpcode::async_cancel_requested:
            result = builder.create_call(forge::ir::i1_type(), "raz_rt_async_cancel_requested", {operand(0)});
            break;
          case MirOpcode::async_frame_cancel: {
            [[maybe_unused]] const auto ignored = builder.create_call(forge::ir::void_type(), "raz_rt_async_frame_cancel", {operand(0)});
            break;
          }
          case MirOpcode::async_await_poll: {
            const auto resume_state = builder.create_constant(forge::ir::i32_type(), instruction.operands.at(2));
            result = builder.create_call(forge::ir::i32_type(), "raz_rt_async_await_poll", {operand(0), operand(1), resume_state});
            break;
          }
          case MirOpcode::async_await_result:
            result = builder.create_call(forge::ir::i64_type(), "raz_rt_async_await_result", {operand(0)});
            break;
          case MirOpcode::async_dispatch:
            diagnostics_.error("D4001", instruction.range, "unexpanded async dispatch reached Forge lowering");
            break;
          case MirOpcode::async_frame_destroy: {
            [[maybe_unused]] const auto ignored = builder.create_call(forge::ir::void_type(), "raz_rt_async_frame_destroy", {operand(0)});
            break;
          }
          case MirOpcode::async_poll_pending:
            result = builder.create_constant(forge::ir::i32_type(), "0");
            break;
          case MirOpcode::async_poll_ready:
            result = builder.create_constant(forge::ir::i32_type(), instruction.operands.at(1));
            break;
          case MirOpcode::jump:
            builder.create_jump(instruction.operands.at(0));
            break;
          case MirOpcode::branch:
            builder.create_branch(operand(0), instruction.operands.at(1), instruction.operands.at(2));
            break;
          case MirOpcode::return_value:
            builder.create_return(operand(0));
            break;
          case MirOpcode::return_void:
            builder.create_return();
            break;
          case MirOpcode::unreachable:
            builder.create_unreachable();
            break;
        }
        if (!instruction.result.empty() && !result.empty()) values[instruction.result] = std::move(result);
      }
    }
  }

  const auto verification = forge::ir::verify_module(module);
  for (const auto& diagnostic : verification) diagnostics_.error("D4000", {}, "Forge IR verification failed: " + diagnostic.message);
  return diagnostics_.has_errors() ? std::string{} : forge::ir::print_module(module);
}

}  // namespace raz::compiler
