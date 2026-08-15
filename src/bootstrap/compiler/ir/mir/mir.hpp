// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "compiler/source/source_location.hpp"

#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

namespace raz::compiler {

enum class MirOpcode : std::uint8_t {
  constant,
  copy,
  stack_allocate,
  load,
  store,
  pointer_offset,
  drop,
  storage_live,
  storage_dead,
  move_value,
  borrow_shared,
  borrow_exclusive,
  borrow_bind,
  place_path,
  add,
  subtract,
  multiply,
  divide,
  remainder,
  bit_and,
  bit_or,
  bit_xor,
  shift_left,
  shift_right,
  numeric_cast,
  equal,
  not_equal,
  less,
  less_equal,
  greater,
  greater_equal,
  call,
  function_address,
  call_indirect,
  callable_create,
  callable_clone,
  callable_invoke,
  callable_destroy,
  trait_object_create,
  trait_object_clone,
  trait_object_invoke,
  trait_object_destroy,
  async_await,
  task_spawn,
  async_frame_create,
  async_poller_set,
  async_frame_future,
  async_state_load,
  async_state_store,
  async_slot_load,
  async_slot_store,
  async_slot_store_owned,
  async_slot_store_owned_bytes,
  async_slot_allocate_owned_bytes,
  async_slot_allocate_projected_bytes,
  async_slot_projection_set,
  async_slot_projection_range_set,
  async_slot_projection_batch_set,
  async_slot_projection_mask,
  async_slot_take,
  async_slot_transfer_projected,
  async_slot_disarm,
  async_slot_address,
  async_result_load,
  async_result_store,
  async_cancel_requested,
  async_frame_cancel,
  async_await_poll,
  async_await_result,
  async_dispatch,
  async_frame_destroy,
  async_poll_pending,
  async_poll_ready,
  jump,
  branch,
  return_value,
  return_void,
  unreachable,
};

struct MirInstruction final {
  MirOpcode opcode = MirOpcode::constant;
  std::string result;
  std::string type_name;
  std::vector<std::string> operands;
  SourceRange range{};
};

struct MirBlock final {
  std::string name;
  std::vector<MirInstruction> instructions;
};

struct MirAsyncFrameSlot final {
  std::string value;
  std::string type_name;
  std::uint32_t slot = 0;
  bool owned = false;
  bool owned_bytes = false;
  std::uint64_t storage_size = 0;
  std::uint32_t storage_alignment = 1;
  std::string cleanup_symbol;
  std::uint32_t projection_count = 0;
};

struct MirAsyncProjectionFlag final {
  std::string root_value;
  std::uint32_t projection = 0;
  std::string flag_place;
};

struct MirSuspensionPoint final {
  std::uint32_t state = 0;
  std::string block;
  std::uint32_t instruction_index = 0;
  std::vector<std::string> live_values;
  std::string awaited_value;
  std::string result_value;
  std::string result_type;
};

enum class MirAsyncStateKind : std::uint8_t {
  entry,
  suspended,
  completed,
  cancelled,
};

struct MirAsyncRegion final {
  std::uint32_t state = 0;
  std::string entry_block;
  std::uint32_t entry_instruction = 0;
  std::vector<std::string> reachable_blocks;
  bool contains_back_edge = false;
};

struct MirAsyncState final {
  std::uint32_t state = 0;
  MirAsyncStateKind kind = MirAsyncStateKind::entry;
  std::string block;
  std::uint32_t instruction_index = 0;
};

enum class MirAsyncGeneratedKind : std::uint8_t {
  constructor,
  poll,
  destroy,
};

struct MirAsyncGeneratedFunction final {
  MirAsyncGeneratedKind kind = MirAsyncGeneratedKind::constructor;
  std::string name;
  std::string return_type = "void";
  std::vector<std::pair<std::string, std::string>> parameters;
  std::vector<MirBlock> blocks;
};

struct MirAsyncStateMachine final {
  std::string frame_type;
  std::string constructor;
  std::string poll;
  std::string destroy;
  std::vector<MirAsyncState> states;
  std::vector<MirAsyncGeneratedFunction> generated_functions;
};


struct MirRegionPoint final {
  std::string block;
  std::uint32_t instruction = 0;
};

struct MirBorrowRegion final {
  std::uint32_t id = 0;
  std::string alias_place;
  std::string alias_path;
  std::string source_place;
  std::string source_path;
  bool mutable_borrow = false;
  std::string begin_block;
  std::uint32_t begin_instruction = 0;
  std::string end_block;
  std::uint32_t end_instruction = 0;
  std::int32_t parent_region = -1;
  bool contains_back_edge = false;
  bool crosses_suspension = false;
  std::vector<MirRegionPoint> uses;
};

struct MirFunction final {
  std::string name;
  std::string return_type = "void";
  std::vector<std::pair<std::string, std::string>> parameters;
  bool is_external = false;
  bool is_async = false;
  std::string external_name;
  std::string abi = "Raz";
  std::vector<MirBlock> blocks;
  std::vector<MirAsyncFrameSlot> async_frame;
  std::vector<MirSuspensionPoint> suspension_points;
  std::vector<MirAsyncProjectionFlag> async_projection_flags;
  std::vector<MirAsyncRegion> async_regions;
  std::vector<MirBorrowRegion> borrow_regions;
  MirAsyncStateMachine async_state_machine;
  SourceRange range{};
};


struct MirAbiField final {
  std::string type_name;
  std::uint64_t offset = 0;
  std::uint64_t size = 0;
  std::uint32_t alignment = 1;
};

enum class MirDispatchAbiKind : std::uint8_t { callable, trait_method };

struct MirDispatchAbi final {
  MirDispatchAbiKind kind = MirDispatchAbiKind::callable;
  std::string owner;
  std::string method;
  std::uint32_t vtable_slot = 0;
  std::string canonical_signature;
  std::uint64_t signature_id = 0;
  std::vector<MirAbiField> arguments;
  std::uint64_t argument_size = 0;
  std::uint32_t argument_alignment = 1;
  std::string result_type = "void";
  std::uint64_t result_size = 0;
  std::uint32_t result_alignment = 1;
};

struct MirAggregateLayout final {
  std::string name;
  std::uint64_t size = 0;
  std::uint32_t alignment = 1;
};

struct MirModule final {
  std::vector<std::string> enum_types;
  std::vector<MirAggregateLayout> aggregate_layouts;
  std::vector<MirDispatchAbi> dispatch_abis;
  std::vector<MirFunction> functions;
  [[nodiscard]] std::vector<std::string> verify() const;
  void dump(std::ostream& stream) const;
};

[[nodiscard]] std::string_view mir_opcode_name(MirOpcode opcode) noexcept;
[[nodiscard]] bool mir_is_terminator(MirOpcode opcode) noexcept;
[[nodiscard]] bool mir_is_ownership_metadata(MirOpcode opcode) noexcept;
void mir_analyze_async_frame(MirFunction& function);
void mir_infer_borrow_regions(MirFunction& function);
void mir_synthesize_async_state_machine(MirFunction& function);
[[nodiscard]] std::string_view mir_async_state_kind_name(MirAsyncStateKind kind) noexcept;
[[nodiscard]] std::string_view mir_async_generated_kind_name(MirAsyncGeneratedKind kind) noexcept;

}  // namespace raz::compiler
