// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "compiler/ir/mir/mir.hpp"

#include <ostream>
#include <algorithm>
#include <charconv>
#include <limits>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace raz::compiler {

std::string_view mir_opcode_name(MirOpcode opcode) noexcept {
  switch (opcode) {
    case MirOpcode::constant: return "const";
    case MirOpcode::copy: return "copy";
    case MirOpcode::stack_allocate: return "stack.alloc";
    case MirOpcode::load: return "load";
    case MirOpcode::store: return "store";
    case MirOpcode::pointer_offset: return "ptr.offset";
    case MirOpcode::drop: return "drop";
    case MirOpcode::storage_live: return "storage.live";
    case MirOpcode::storage_dead: return "storage.dead";
    case MirOpcode::move_value: return "move";
    case MirOpcode::borrow_shared: return "borrow.shared";
    case MirOpcode::borrow_exclusive: return "borrow.exclusive";
    case MirOpcode::borrow_bind: return "borrow.bind";
    case MirOpcode::place_path: return "place.path";
    case MirOpcode::add: return "add";
    case MirOpcode::subtract: return "sub";
    case MirOpcode::multiply: return "mul";
    case MirOpcode::divide: return "div";
    case MirOpcode::remainder: return "rem";
    case MirOpcode::bit_and: return "bit.and";
    case MirOpcode::bit_or: return "bit.or";
    case MirOpcode::bit_xor: return "bit.xor";
    case MirOpcode::shift_left: return "shl";
    case MirOpcode::shift_right: return "shr";
    case MirOpcode::numeric_cast: return "cast.numeric";
    case MirOpcode::equal: return "eq";
    case MirOpcode::not_equal: return "ne";
    case MirOpcode::less: return "lt";
    case MirOpcode::less_equal: return "le";
    case MirOpcode::greater: return "gt";
    case MirOpcode::greater_equal: return "ge";
    case MirOpcode::call: return "call";
    case MirOpcode::function_address: return "func.address";
    case MirOpcode::call_indirect: return "call.indirect";
    case MirOpcode::callable_create: return "callable.create";
    case MirOpcode::callable_clone: return "callable.clone";
    case MirOpcode::callable_invoke: return "callable.invoke";
    case MirOpcode::callable_destroy: return "callable.destroy";
    case MirOpcode::trait_object_create: return "trait.object.create";
    case MirOpcode::trait_object_clone: return "trait.object.clone";
    case MirOpcode::trait_object_invoke: return "trait.object.invoke";
    case MirOpcode::trait_object_destroy: return "trait.object.destroy";
    case MirOpcode::async_await: return "async.await";
    case MirOpcode::task_spawn: return "task.spawn";
    case MirOpcode::async_frame_create: return "async.frame.create";
    case MirOpcode::async_poller_set: return "async.poller.set";
    case MirOpcode::async_frame_future: return "async.frame.future";
    case MirOpcode::async_state_load: return "async.state.load";
    case MirOpcode::async_state_store: return "async.state.store";
    case MirOpcode::async_slot_load: return "async.slot.load";
    case MirOpcode::async_slot_store: return "async.slot.store";
    case MirOpcode::async_slot_store_owned: return "async.slot.store.owned";
    case MirOpcode::async_slot_store_owned_bytes: return "async.slot.store.owned.bytes";
    case MirOpcode::async_slot_allocate_owned_bytes: return "async.slot.allocate.owned.bytes";
    case MirOpcode::async_slot_allocate_projected_bytes: return "async.slot.allocate.projected.bytes";
    case MirOpcode::async_slot_projection_set: return "async.slot.projection.set";
    case MirOpcode::async_slot_projection_range_set: return "async.slot.projection.range.set";
    case MirOpcode::async_slot_projection_batch_set: return "async.slot.projection.batch.set";
    case MirOpcode::async_slot_projection_mask: return "async.slot.projection.mask";
    case MirOpcode::async_slot_take: return "async.slot.take";
    case MirOpcode::async_slot_transfer_projected: return "async.slot.transfer.projected";
    case MirOpcode::async_slot_disarm: return "async.slot.disarm";
    case MirOpcode::async_slot_address: return "async.slot.address";
    case MirOpcode::async_result_load: return "async.result.load";
    case MirOpcode::async_result_store: return "async.result.store";
    case MirOpcode::async_cancel_requested: return "async.cancel.requested";
    case MirOpcode::async_frame_cancel: return "async.frame.cancel";
    case MirOpcode::async_await_poll: return "async.await.poll";
    case MirOpcode::async_await_result: return "async.await.result";
    case MirOpcode::async_dispatch: return "async.dispatch";
    case MirOpcode::async_frame_destroy: return "async.frame.destroy";
    case MirOpcode::async_poll_pending: return "async.poll.pending";
    case MirOpcode::async_poll_ready: return "async.poll.ready";
    case MirOpcode::jump: return "jump";
    case MirOpcode::branch: return "branch";
    case MirOpcode::return_value: return "return";
    case MirOpcode::return_void: return "return";
    case MirOpcode::unreachable: return "unreachable";
  }
  return "unknown";
}

void mir_analyze_async_frame(MirFunction& function) {
  function.async_frame.clear();
  function.suspension_points.clear();
  function.async_regions.clear();
  if (!function.is_async || function.is_external) return;

  struct ValueLifetime final {
    std::string type_name;
    std::size_t defined_at = 0;
    std::size_t last_used_at = 0;
  };
  std::unordered_map<std::string, ValueLifetime> lifetimes;
  std::size_t position = 0;
  for (const auto& [type_name, parameter] : function.parameters) {
    lifetimes.emplace(parameter, ValueLifetime{type_name, 0, 0});
  }

  for (const auto& block : function.blocks) {
    for (const auto& instruction : block.instructions) {
      ++position;
      for (const auto& operand : instruction.operands) {
        if (auto found = lifetimes.find(operand); found != lifetimes.end()) found->second.last_used_at = position;
      }
      if (!instruction.result.empty()) {
        lifetimes[instruction.result] = ValueLifetime{instruction.type_name, position, position};
      }
    }
  }

  position = 0;
  std::unordered_set<std::string> framed;
  // Poll helpers only receive the frame, so constructor parameters must be
  // persisted even when they are consumed before the first suspension.
  for (const auto& [type_name, parameter] : function.parameters) {
    if (!framed.insert(parameter).second) continue;
    function.async_frame.push_back({parameter, type_name, static_cast<std::uint32_t>(function.async_frame.size()), false, false, 0, 1, {}});
  }
  std::uint32_t state = 1;
  for (const auto& block : function.blocks) {
    for (std::size_t index = 0; index < block.instructions.size(); ++index) {
      const auto& instruction = block.instructions[index];
      ++position;
      if (instruction.opcode != MirOpcode::async_await) continue;
      MirSuspensionPoint point;
      point.state = state++;
      point.block = block.name;
      point.instruction_index = static_cast<std::uint32_t>(index);
      point.awaited_value = instruction.operands.empty() ? std::string{} : instruction.operands.front();
      point.result_value = instruction.result;
      point.result_type = instruction.type_name;
      for (const auto& [value, lifetime] : lifetimes) {
        if (lifetime.defined_at < position && lifetime.last_used_at > position) point.live_values.push_back(value);
      }
      std::sort(point.live_values.begin(), point.live_values.end());
      if (!point.awaited_value.empty() &&
          std::find(point.live_values.begin(), point.live_values.end(), point.awaited_value) == point.live_values.end()) {
        point.live_values.push_back(point.awaited_value);
        std::sort(point.live_values.begin(), point.live_values.end());
      }
      for (const auto& value : point.live_values) {
        if (!framed.insert(value).second) continue;
        const auto& lifetime = lifetimes.at(value);
        function.async_frame.push_back({value, lifetime.type_name, static_cast<std::uint32_t>(function.async_frame.size()), false, false, 0, 1, {}});
      }
      function.suspension_points.push_back(std::move(point));
    }
  }

  std::sort(function.async_frame.begin(), function.async_frame.end(), [](const auto& lhs, const auto& rhs) {
    return lhs.slot < rhs.slot;
  });

  std::unordered_map<std::string, std::size_t> block_index;
  for (std::size_t index = 0; index < function.blocks.size(); ++index) block_index.emplace(function.blocks[index].name, index);
  std::vector<std::vector<std::size_t>> successors(function.blocks.size());
  for (std::size_t index = 0; index < function.blocks.size(); ++index) {
    const auto& instructions = function.blocks[index].instructions;
    if (instructions.empty()) continue;
    const auto& terminator = instructions.back();
    auto add_successor = [&](const std::string& name) {
      if (const auto found = block_index.find(name); found != block_index.end()) successors[index].push_back(found->second);
    };
    if (terminator.opcode == MirOpcode::jump && !terminator.operands.empty()) add_successor(terminator.operands[0]);
    if (terminator.opcode == MirOpcode::branch && terminator.operands.size() == 3) {
      add_successor(terminator.operands[1]);
      add_successor(terminator.operands[2]);
    }
  }
  auto build_region = [&](std::uint32_t region_state, const std::string& entry_block, std::uint32_t entry_instruction) {
    MirAsyncRegion region;
    region.state = region_state;
    region.entry_block = entry_block;
    region.entry_instruction = entry_instruction;
    const auto entry = block_index.find(entry_block);
    if (entry == block_index.end()) {
      function.async_regions.push_back(std::move(region));
      return;
    }
    std::vector<std::size_t> stack{entry->second};
    std::unordered_set<std::size_t> visited;
    while (!stack.empty()) {
      const auto current = stack.back();
      stack.pop_back();
      if (!visited.insert(current).second) continue;
      region.reachable_blocks.push_back(function.blocks[current].name);
      for (const auto next : successors[current]) {
        if (next <= current) region.contains_back_edge = true;
        if (!visited.contains(next)) stack.push_back(next);
      }
    }
    std::sort(region.reachable_blocks.begin(), region.reachable_blocks.end());
    function.async_regions.push_back(std::move(region));
  };
  if (!function.blocks.empty()) build_region(0, function.blocks.front().name, 0);
  for (const auto& point : function.suspension_points) build_region(point.state, point.block, point.instruction_index + 1);
}

std::string_view mir_async_state_kind_name(MirAsyncStateKind kind) noexcept {
  switch (kind) {
    case MirAsyncStateKind::entry: return "entry";
    case MirAsyncStateKind::suspended: return "suspended";
    case MirAsyncStateKind::completed: return "completed";
    case MirAsyncStateKind::cancelled: return "cancelled";
  }
  return "unknown";
}

std::string_view mir_async_generated_kind_name(MirAsyncGeneratedKind kind) noexcept {
  switch (kind) {
    case MirAsyncGeneratedKind::constructor: return "constructor";
    case MirAsyncGeneratedKind::poll: return "poll";
    case MirAsyncGeneratedKind::destroy: return "destroy";
  }
  return "unknown";
}

void mir_synthesize_async_state_machine(MirFunction& function) {
  function.async_state_machine = {};
  if (!function.is_async || function.is_external) return;

  auto& machine = function.async_state_machine;
  machine.frame_type = function.name + "$async_frame";
  machine.constructor = function.name + "$async_new";
  machine.poll = function.name + "$async_poll";
  machine.destroy = function.name + "$async_drop";
  machine.states.push_back({0, MirAsyncStateKind::entry, function.blocks.empty() ? std::string{} : function.blocks.front().name, 0});
  for (const auto& point : function.suspension_points) {
    machine.states.push_back({point.state, MirAsyncStateKind::suspended, point.block, point.instruction_index + 1});
  }
  const auto terminal = static_cast<std::uint32_t>(function.suspension_points.size() + 1);
  const auto cancelled_state = terminal + 1;
  machine.states.push_back({terminal, MirAsyncStateKind::completed, {}, 0});
  machine.states.push_back({cancelled_state, MirAsyncStateKind::cancelled, {}, 0});

  const auto slot_for = [&](const std::string& value) -> const MirAsyncFrameSlot* {
    const auto found = std::find_if(function.async_frame.begin(), function.async_frame.end(),
                                    [&](const MirAsyncFrameSlot& slot) { return slot.value == value; });
    return found == function.async_frame.end() ? nullptr : &*found;
  };

  MirAsyncGeneratedFunction constructor;
  constructor.kind = MirAsyncGeneratedKind::constructor;
  constructor.name = machine.constructor;
  constructor.return_type = machine.frame_type + "*mut";
  constructor.parameters = function.parameters;
  MirBlock constructor_entry{"entry", {}};
  constructor_entry.instructions.push_back({MirOpcode::async_frame_create, "frame", constructor.return_type,
                                            {machine.frame_type, std::to_string(function.async_frame.size())}, {}});
  constructor_entry.instructions.push_back({MirOpcode::async_state_store, {}, "u32", {"frame", "0"}, {}});
  constructor_entry.instructions.push_back({MirOpcode::async_poller_set, {}, "void", {"frame", machine.poll}, {}});
  for (const auto& [type_name, parameter] : function.parameters) {
    if (const auto* slot = slot_for(parameter); slot != nullptr) {
      const auto opcode = slot->owned_bytes ? MirOpcode::async_slot_store_owned_bytes
                                      : (slot->owned ? MirOpcode::async_slot_store_owned : MirOpcode::async_slot_store);
      constructor_entry.instructions.push_back({opcode, {}, type_name,
                                                slot->owned_bytes
                                                    ? std::vector<std::string>{"frame", std::to_string(slot->slot), parameter,
                                                                               std::to_string(slot->storage_size),
                                                                               std::to_string(slot->storage_alignment), slot->cleanup_symbol}
                                                : slot->owned
                                                    ? std::vector<std::string>{"frame", std::to_string(slot->slot), parameter, slot->cleanup_symbol}
                                                    : std::vector<std::string>{"frame", std::to_string(slot->slot), parameter}, {}});
    }
  }
  constructor_entry.instructions.push_back({MirOpcode::return_value, {}, constructor.return_type, {"frame"}, {}});
  constructor.blocks.push_back(std::move(constructor_entry));
  machine.generated_functions.push_back(std::move(constructor));

  MirAsyncGeneratedFunction poll;
  poll.kind = MirAsyncGeneratedKind::poll;
  poll.name = machine.poll;
  poll.return_type = "i32";
  poll.parameters.push_back({machine.frame_type + "*mut", "frame"});
  MirBlock poll_entry{"entry", {}};
  poll_entry.instructions.push_back({MirOpcode::async_state_load, "state", "u32", {"frame"}, {}});
  poll_entry.instructions.push_back({MirOpcode::jump, {}, {}, {"dispatch.0"}, {}});
  poll.blocks.push_back(std::move(poll_entry));
  for (std::size_t index = 0; index < machine.states.size(); ++index) {
    const auto& state = machine.states[index];
    MirBlock dispatch_block{"dispatch." + std::to_string(index), {}};
    dispatch_block.instructions.push_back({MirOpcode::constant, "expected." + std::to_string(index), "u32", {std::to_string(state.state)}, {}});
    dispatch_block.instructions.push_back({MirOpcode::equal, "matches." + std::to_string(index), "u32", {"state", "expected." + std::to_string(index)}, {}});
    const auto fallback = index + 1 < machine.states.size() ? "dispatch." + std::to_string(index + 1) : "invalid.state";
    dispatch_block.instructions.push_back({MirOpcode::branch, {}, {}, {"matches." + std::to_string(index), "state." + std::to_string(state.state), fallback}, {}});
    poll.blocks.push_back(std::move(dispatch_block));
  }
  MirBlock invalid_state{"invalid.state", {}};
  invalid_state.instructions.push_back({MirOpcode::unreachable, {}, {}, {}, {}});
  poll.blocks.push_back(std::move(invalid_state));

  // Async bodies are partitioned into executable state regions. Straight-line
  // bodies and general MIR control-flow graphs share the same cloning path.
  std::unordered_set<std::string> promoted_allocations;
  for (const auto& block : function.blocks) {
    for (const auto& instruction : block.instructions) {
      if (instruction.opcode == MirOpcode::stack_allocate && slot_for(instruction.result) != nullptr) {
        promoted_allocations.insert(instruction.result);
      }
    }
  }

  const auto emit_terminal = [&](MirBlock& block, const MirInstruction& instruction,
                                 const std::unordered_map<std::string, std::string>& aliases) {
    auto rewrite = [&](const std::string& operand) {
      const auto found = aliases.find(operand);
      return found == aliases.end() ? operand : found->second;
    };
    if (instruction.opcode == MirOpcode::return_value) {
      block.instructions.push_back({MirOpcode::async_result_store, {}, function.return_type,
                                    {"frame", rewrite(instruction.operands.front())}, instruction.range});
    } else {
      block.instructions.push_back({MirOpcode::constant, "void.result", "i64", {"0"}, instruction.range});
      block.instructions.push_back({MirOpcode::async_result_store, {}, "i64", {"frame", "void.result"}, instruction.range});
    }
    block.instructions.push_back({MirOpcode::async_state_store, {}, "u32", {"frame", std::to_string(terminal)}, {}});
    block.instructions.push_back({MirOpcode::async_poll_ready, "status.ready", "i32", {"frame", "1"}, {}});
    block.instructions.push_back({MirOpcode::return_value, {}, "i32", {"status.ready"}, {}});
  };

  // Emit active state guards first. Work/resume blocks are emitted below.
  for (const auto& state : machine.states) {
    MirBlock state_block{"state." + std::to_string(state.state), {}};
    if (state.kind == MirAsyncStateKind::entry || state.kind == MirAsyncStateKind::suspended) {
      state_block.instructions.push_back({MirOpcode::async_cancel_requested, "cancelled." + std::to_string(state.state), "bool", {"frame"}, {}});
      state_block.instructions.push_back({MirOpcode::branch, {}, {}, {"cancelled." + std::to_string(state.state),
                                          "state." + std::to_string(cancelled_state),
                                          "state." + std::to_string(state.state) + ".work"}, {}});
    } else if (state.kind == MirAsyncStateKind::completed) {
      state_block.instructions.push_back({MirOpcode::async_result_load, "result", function.return_type, {"frame"}, {}});
      state_block.instructions.push_back({MirOpcode::async_poll_ready, "status.completed", "i32", {"frame", "1"}, {}});
      state_block.instructions.push_back({MirOpcode::return_value, {}, "i32", {"status.completed"}, {}});
    } else {
      state_block.instructions.push_back({MirOpcode::async_frame_cancel, {}, "void", {"frame"}, {}});
      state_block.instructions.push_back({MirOpcode::async_poll_ready, "status.cancelled", "i32", {"frame", "-1"}, {}});
      state_block.instructions.push_back({MirOpcode::return_value, {}, "i32", {"status.cancelled"}, {}});
    }
    poll.blocks.push_back(std::move(state_block));
  }

  std::unordered_map<std::string, const MirBlock*> source_blocks;
  for (const auto& block : function.blocks) source_blocks.emplace(block.name, &block);
  const auto point_at = [&](const std::string& block, std::size_t instruction) -> const MirSuspensionPoint* {
    const auto found = std::find_if(function.suspension_points.begin(), function.suspension_points.end(),
      [&](const MirSuspensionPoint& point) { return point.block == block && point.instruction_index == instruction; });
    return found == function.suspension_points.end() ? nullptr : &*found;
  };

  const auto clone_region = [&](const MirAsyncRegion& region) {
    const auto state = region.state;
    const auto suffix = "$s" + std::to_string(state);
    const auto label_for = [&](const std::string& block) {
      return "state." + std::to_string(state) + ".cfg." + block;
    };

    std::unordered_map<std::string, std::string> aliases;
    for (const auto& block_name : region.reachable_blocks) {
      const auto found = source_blocks.find(block_name);
      if (found == source_blocks.end()) continue;
      for (const auto& instruction : found->second->instructions) {
        if (!instruction.result.empty()) aliases.emplace(instruction.result, instruction.result + suffix + "$" + block_name);
      }
    }

    MirBlock work{"state." + std::to_string(state) + ".work", {}};
    if (state == 0) {
      for (const auto& [type_name, parameter] : function.parameters) {
        if (const auto* slot = slot_for(parameter); slot != nullptr) {
          const auto loaded = parameter + suffix;
          work.instructions.push_back({MirOpcode::async_slot_load, loaded, type_name,
                                       {"frame", std::to_string(slot->slot)}, {}});
          aliases[parameter] = loaded;
        }
      }
    } else {
      const auto& point = function.suspension_points[state - 1];
      const auto* awaited_slot = slot_for(point.awaited_value);
      const auto awaited_handle = point.awaited_value + suffix + "$poll";
      if (awaited_slot != nullptr) {
        work.instructions.push_back({MirOpcode::async_slot_load, awaited_handle, awaited_slot->type_name,
                                     {"frame", std::to_string(awaited_slot->slot)}, {}});
      }
      work.instructions.push_back({MirOpcode::async_await_poll, "await.status" + suffix, "i32",
                                   {"frame", awaited_slot == nullptr ? point.awaited_value : awaited_handle,
                                    std::to_string(state)}, {}});
      work.instructions.push_back({MirOpcode::constant, "await.ready" + suffix, "i32", {"1"}, {}});
      work.instructions.push_back({MirOpcode::equal, "await.is_ready" + suffix, "i32",
                                   {"await.status" + suffix, "await.ready" + suffix}, {}});
      work.instructions.push_back({MirOpcode::branch, {}, {}, {"await.is_ready" + suffix,
                                   "state." + std::to_string(state) + ".resume",
                                   "state." + std::to_string(state) + ".pending"}, {}});
      poll.blocks.push_back(std::move(work));
      work = MirBlock{"state." + std::to_string(state) + ".resume", {}};
      for (const auto& value : point.live_values) {
        const auto* slot = slot_for(value);
        if (slot == nullptr) continue;
        const auto loaded = value + suffix;
        if (promoted_allocations.contains(value)) {
          if (slot->owned_bytes) {
            work.instructions.push_back({MirOpcode::async_slot_transfer_projected, loaded, slot->type_name + "*mut",
                                         {"frame", std::to_string(slot->slot)}, {}});
          } else {
            if (slot->owned) {
              work.instructions.push_back({MirOpcode::async_slot_disarm, {}, slot->type_name,
                                           {"frame", std::to_string(slot->slot)}, {}});
            }
            work.instructions.push_back({MirOpcode::async_slot_address, loaded, slot->type_name + "*mut",
                                         {"frame", std::to_string(slot->slot)}, {}});
          }
        } else {
          if (slot->owned_bytes) {
            work.instructions.push_back({MirOpcode::async_slot_transfer_projected, loaded, slot->type_name,
                                         {"frame", std::to_string(slot->slot)}, {}});
          } else {
            work.instructions.push_back({slot->owned ? MirOpcode::async_slot_take : MirOpcode::async_slot_load, loaded, slot->type_name,
                                         {"frame", std::to_string(slot->slot)}, {}});
          }
        }
        aliases[value] = loaded;
      }
      const auto await_result = point.result_value + suffix;
      work.instructions.push_back({MirOpcode::async_await_result, await_result, point.result_type, {"frame"}, {}});
      aliases[point.result_value] = await_result;
    }

    const auto rewrite_value = [&](const std::string& operand) {
      const auto found = aliases.find(operand);
      return found == aliases.end() ? operand : found->second;
    };
    const auto rewrite_label = [&](const std::string& target) { return label_for(target); };

    const auto clone_instructions = [&](MirBlock& destination, const MirBlock& source, std::size_t begin) {
      bool terminated = false;
      for (std::size_t index = begin; index < source.instructions.size(); ++index) {
        const auto& original = source.instructions[index];
        if (original.opcode == MirOpcode::return_value || original.opcode == MirOpcode::return_void) {
          emit_terminal(destination, original, aliases);
          terminated = true;
          break;
        }
        if (original.opcode == MirOpcode::async_await) {
          const auto* point = point_at(source.name, index);
          if (point == nullptr) {
            destination.instructions.push_back({MirOpcode::unreachable, {}, {}, {}, original.range});
            terminated = true;
            break;
          }
          for (const auto& value : point->live_values) {
            const auto* slot = slot_for(value);
            if (slot == nullptr) continue;
            if (slot->owned_bytes && slot->projection_count != 0) {
              std::vector<const MirAsyncProjectionFlag*> projections;
              for (const auto& projection : function.async_projection_flags) {
                if (projection.root_value == value && projection.projection < slot->projection_count)
                  projections.push_back(&projection);
              }
              std::sort(projections.begin(), projections.end(), [](const auto* left, const auto* right) {
                return left->projection < right->projection;
              });
              struct ProjectionRun final {
                std::uint32_t first = 0;
                std::uint32_t count = 0;
                std::string active;
              };
              std::vector<ProjectionRun> runs;
              for (std::size_t projection_index = 0; projection_index < projections.size();) {
                const auto* first = projections[projection_index];
                auto run_end = projection_index + 1;
                while (run_end < projections.size() &&
                       projections[run_end]->projection == projections[run_end - 1]->projection + 1 &&
                       projections[run_end]->flag_place == first->flag_place) ++run_end;
                const auto active = value + suffix + "$projection." + std::to_string(first->projection) + "$" + source.name;
                destination.instructions.push_back({MirOpcode::load, active, "bool",
                                                    {rewrite_value(first->flag_place)}, original.range});
                runs.push_back({first->projection, static_cast<std::uint32_t>(run_end - projection_index), active});
                projection_index = run_end;
              }
              if (runs.size() == 1) {
                const auto& run = runs.front();
                const auto opcode = run.count == 1 ? MirOpcode::async_slot_projection_set
                                                   : MirOpcode::async_slot_projection_range_set;
                auto operands = std::vector<std::string>{"frame", std::to_string(slot->slot),
                                                         std::to_string(run.first)};
                if (run.count != 1) operands.push_back(std::to_string(run.count));
                operands.push_back(run.active);
                destination.instructions.push_back({opcode, {}, "bool", std::move(operands), original.range});
              } else if (!runs.empty()) {
                std::vector<std::string> operands{"frame", std::to_string(slot->slot), std::to_string(runs.size())};
                for (const auto& run : runs) {
                  operands.push_back(std::to_string(run.first));
                  operands.push_back(std::to_string(run.count));
                  operands.push_back(run.active);
                }
                const auto changed = value + suffix + "$projection.changed$" + source.name;
                destination.instructions.push_back({MirOpcode::async_slot_projection_batch_set, changed, "i64",
                                                    std::move(operands), original.range});
              }
            }
            auto stored_value = rewrite_value(value);
            if (promoted_allocations.contains(value)) {
              if (slot->owned_bytes) continue;
              if (!slot->owned) continue;
              stored_value = value + suffix + "$owned.spill$" + source.name;
              destination.instructions.push_back({MirOpcode::load, stored_value, slot->type_name,
                                                  {rewrite_value(value)}, original.range});
            }
            const auto store_opcode = slot->owned_bytes ? MirOpcode::async_slot_store_owned_bytes
                                                       : (slot->owned ? MirOpcode::async_slot_store_owned : MirOpcode::async_slot_store);
            destination.instructions.push_back({store_opcode, {}, slot->type_name,
                                                slot->owned_bytes
                                                    ? std::vector<std::string>{"frame", std::to_string(slot->slot), stored_value,
                                                                               std::to_string(slot->storage_size),
                                                                               std::to_string(slot->storage_alignment), slot->cleanup_symbol}
                                                : slot->owned
                                                    ? std::vector<std::string>{"frame", std::to_string(slot->slot), stored_value, slot->cleanup_symbol}
                                                    : std::vector<std::string>{"frame", std::to_string(slot->slot), stored_value}, original.range});
          }
          destination.instructions.push_back({MirOpcode::async_state_store, {}, "u32",
                                              {"frame", std::to_string(point->state)}, original.range});
          destination.instructions.push_back({MirOpcode::async_await_poll, "await.status.start" + suffix + "$" + source.name, "i32",
                                              {"frame", rewrite_value(point->awaited_value), std::to_string(point->state)}, original.range});
          destination.instructions.push_back({MirOpcode::constant, "await.ready.start" + suffix + "$" + source.name, "i32", {"1"}, original.range});
          destination.instructions.push_back({MirOpcode::equal, "await.is_ready.start" + suffix + "$" + source.name, "i32",
                                              {"await.status.start" + suffix + "$" + source.name,
                                               "await.ready.start" + suffix + "$" + source.name}, original.range});
          destination.instructions.push_back({MirOpcode::branch, {}, {}, {"await.is_ready.start" + suffix + "$" + source.name,
                                              "state." + std::to_string(point->state) + ".resume",
                                              "state." + std::to_string(point->state) + ".pending"}, original.range});
          terminated = true;
          break;
        }
        MirInstruction cloned = original;
        if (cloned.opcode == MirOpcode::jump && !cloned.operands.empty()) {
          cloned.operands[0] = rewrite_label(cloned.operands[0]);
        } else if (cloned.opcode == MirOpcode::branch && cloned.operands.size() == 3) {
          cloned.operands[0] = rewrite_value(cloned.operands[0]);
          cloned.operands[1] = rewrite_label(cloned.operands[1]);
          cloned.operands[2] = rewrite_label(cloned.operands[2]);
        } else {
          for (auto& operand : cloned.operands) operand = rewrite_value(operand);
        }
        if (!cloned.result.empty()) {
          const auto original_result = cloned.result;
          if (cloned.opcode == MirOpcode::stack_allocate && promoted_allocations.contains(original_result)) {
            const auto* slot = slot_for(original_result);
            if (slot->owned_bytes) {
              cloned.opcode = slot->projection_count == 0 ? MirOpcode::async_slot_allocate_owned_bytes
                                                         : MirOpcode::async_slot_allocate_projected_bytes;
              cloned.type_name = original.type_name + "*mut";
              cloned.operands = {"frame", std::to_string(slot->slot), std::to_string(slot->storage_size),
                                 std::to_string(slot->storage_alignment), slot->cleanup_symbol};
              if (slot->projection_count != 0) {
                const auto mask = slot->projection_count >= 64 ? ~std::uint64_t{0}
                                                               : ((std::uint64_t{1} << slot->projection_count) - 1);
                cloned.operands.push_back(std::to_string(mask));
                cloned.operands.push_back(std::to_string(slot->projection_count));
              }
            } else {
              cloned.opcode = MirOpcode::async_slot_address;
              cloned.type_name = original.type_name + "*mut";
              cloned.operands = {"frame", std::to_string(slot->slot)};
            }
          }
          cloned.result = aliases.at(original_result);
        }
        destination.instructions.push_back(std::move(cloned));
        if (original.opcode == MirOpcode::jump || original.opcode == MirOpcode::branch || original.opcode == MirOpcode::unreachable) {
          terminated = true;
          break;
        }
      }
      if (!terminated) {
        destination.instructions.push_back({MirOpcode::async_poll_pending, "status.pending" + suffix + "$" + source.name, "i32",
                                            {"frame", std::to_string(state)}, {}});
        destination.instructions.push_back({MirOpcode::return_value, {}, "i32", {"status.pending" + suffix + "$" + source.name}, {}});
      }
    };

    const auto entry_found = source_blocks.find(region.entry_block);
    if (entry_found == source_blocks.end()) {
      work.instructions.push_back({MirOpcode::unreachable, {}, {}, {}, {}});
    } else {
      clone_instructions(work, *entry_found->second, region.entry_instruction);
    }
    poll.blocks.push_back(std::move(work));

    // Clone only blocks that are actual internal branch/back-edge targets.
    // Emitting every reachable block would create predecessor-free blocks, and
    // Forge correctly rejects parameter uses in those unreachable islands.
    std::unordered_set<std::string> branch_targets;
    for (const auto& block_name : region.reachable_blocks) {
      const auto found = source_blocks.find(block_name);
      if (found == source_blocks.end() || found->second->instructions.empty()) continue;
      const auto& terminator = found->second->instructions.back();
      if (terminator.opcode == MirOpcode::jump && !terminator.operands.empty()) {
        branch_targets.insert(terminator.operands[0]);
      } else if (terminator.opcode == MirOpcode::branch && terminator.operands.size() == 3) {
        branch_targets.insert(terminator.operands[1]);
        branch_targets.insert(terminator.operands[2]);
      }
    }
    for (const auto& block_name : region.reachable_blocks) {
      if (!branch_targets.contains(block_name)) continue;
      const auto found = source_blocks.find(block_name);
      if (found == source_blocks.end()) continue;
      MirBlock cloned{label_for(block_name), {}};
      clone_instructions(cloned, *found->second, 0);
      poll.blocks.push_back(std::move(cloned));
    }
  };

  // All CFG regions now use executable cloning. Stack allocations whose
  // addresses cross a suspension point are promoted to stable frame slots;
  // region-local allocations remain ordinary poll-stack allocations because
  // their addresses cannot escape the current poll invocation.
  for (const auto& region : function.async_regions) clone_region(region);
  for (const auto& point : function.suspension_points) {
    MirBlock pending{"state." + std::to_string(point.state) + ".pending", {}};
    pending.instructions.push_back({MirOpcode::async_poll_pending, "status.wait$s" + std::to_string(point.state), "i32",
                                    {"frame", std::to_string(point.state)}, {}});
    pending.instructions.push_back({MirOpcode::return_value, {}, "i32", {"status.wait$s" + std::to_string(point.state)}, {}});
    poll.blocks.push_back(std::move(pending));
  }
  machine.generated_functions.push_back(std::move(poll));

  MirAsyncGeneratedFunction destroy;
  destroy.kind = MirAsyncGeneratedKind::destroy;
  destroy.name = machine.destroy;
  destroy.parameters.push_back({machine.frame_type + "*mut", "frame"});
  MirBlock destroy_entry{"entry", {}};
  destroy_entry.instructions.push_back({MirOpcode::async_frame_destroy, {}, machine.frame_type, {"frame"}, {}});
  destroy_entry.instructions.push_back({MirOpcode::return_void, {}, {}, {}, {}});
  destroy.blocks.push_back(std::move(destroy_entry));
  machine.generated_functions.push_back(std::move(destroy));
}

void mir_infer_borrow_regions(MirFunction& function) {
  function.borrow_regions.clear();
  if (function.blocks.empty()) return;

  std::unordered_map<std::string, std::string> logical_paths;
  std::unordered_map<std::string, std::pair<std::size_t, std::size_t>> definitions;
  std::unordered_map<std::string, std::size_t> blocks_by_name;
  for (std::size_t block_index = 0; block_index < function.blocks.size(); ++block_index) {
    blocks_by_name.emplace(function.blocks[block_index].name, block_index);
    for (std::size_t instruction_index = 0; instruction_index < function.blocks[block_index].instructions.size(); ++instruction_index) {
      const auto& instruction = function.blocks[block_index].instructions[instruction_index];
      if (!instruction.result.empty()) definitions[instruction.result] = {block_index, instruction_index};
      if (instruction.opcode == MirOpcode::place_path && instruction.operands.size() == 2) {
        logical_paths[instruction.operands[0]] = instruction.operands[1];
      }
    }
  }

  std::vector<std::vector<std::size_t>> successors(function.blocks.size());
  for (std::size_t block_index = 0; block_index < function.blocks.size(); ++block_index) {
    if (function.blocks[block_index].instructions.empty()) continue;
    const auto& terminator = function.blocks[block_index].instructions.back();
    const auto append = [&](const std::string& name) {
      if (const auto found = blocks_by_name.find(name); found != blocks_by_name.end()) successors[block_index].push_back(found->second);
    };
    if (terminator.opcode == MirOpcode::jump && terminator.operands.size() == 1) append(terminator.operands[0]);
    if (terminator.opcode == MirOpcode::branch && terminator.operands.size() == 3) {
      append(terminator.operands[1]);
      append(terminator.operands[2]);
    }
  }

  struct Binding final {
    MirBorrowRegion region;
    std::size_t block = 0;
    std::size_t instruction = 0;
  };
  std::vector<Binding> bindings;
  for (std::size_t block_index = 0; block_index < function.blocks.size(); ++block_index) {
    for (std::size_t instruction_index = 0; instruction_index < function.blocks[block_index].instructions.size(); ++instruction_index) {
      const auto& instruction = function.blocks[block_index].instructions[instruction_index];
      if (instruction.opcode != MirOpcode::borrow_bind || instruction.operands.size() < 2) continue;
      Binding binding;
      binding.region.id = static_cast<std::uint32_t>(bindings.size());
      binding.region.alias_place = instruction.operands[0];
      binding.region.source_place = instruction.operands[1];
      binding.region.source_path = instruction.operands.size() >= 3 ? instruction.operands[2] : std::string{};
      binding.region.alias_path = instruction.operands.size() >= 4 ? instruction.operands[3] : logical_paths[binding.region.alias_place];
      binding.region.mutable_borrow = instruction.type_name.ends_with("&mut");
      binding.region.begin_block = function.blocks[block_index].name;
      binding.region.begin_instruction = static_cast<std::uint32_t>(instruction_index);
      binding.region.end_block = binding.region.begin_block;
      binding.region.end_instruction = binding.region.begin_instruction;
      binding.block = block_index;
      binding.instruction = instruction_index;
      bindings.push_back(std::move(binding));
    }
  }

  // Track the values derived from each reference slot and infer the region from
  // actual CFG uses. This is intentionally MIR-based: source lexical scope does
  // not determine the end of a loan.
  std::vector<std::unordered_set<std::string>> derived(bindings.size());
  for (std::size_t index = 0; index < bindings.size(); ++index) derived[index].insert(bindings[index].region.alias_place);

  bool changed = true;
  while (changed) {
    changed = false;
    for (std::size_t region_index = 0; region_index < bindings.size(); ++region_index) {
      auto& values = derived[region_index];
      for (const auto& block : function.blocks) {
        for (const auto& instruction : block.instructions) {
          bool consumes = false;
          for (const auto& operand : instruction.operands) {
            if (values.contains(operand)) { consumes = true; break; }
          }
          if (!consumes || instruction.result.empty()) continue;
          switch (instruction.opcode) {
            case MirOpcode::load:
            case MirOpcode::copy:
            case MirOpcode::pointer_offset:
            case MirOpcode::numeric_cast:
              changed = values.insert(instruction.result).second || changed;
              break;
            default: break;
          }
        }
      }
    }
  }

  for (std::size_t region_index = 0; region_index < bindings.size(); ++region_index) {
    auto& binding = bindings[region_index];
    auto& region = binding.region;
    const auto& values = derived[region_index];
    std::vector<std::size_t> use_blocks;
    for (std::size_t block_index = 0; block_index < function.blocks.size(); ++block_index) {
      const auto& block = function.blocks[block_index];
      for (std::size_t instruction_index = 0; instruction_index < block.instructions.size(); ++instruction_index) {
        if (block_index == binding.block && instruction_index <= binding.instruction) continue;
        const auto& instruction = block.instructions[instruction_index];
        if (instruction.opcode == MirOpcode::place_path || instruction.opcode == MirOpcode::borrow_bind) continue;
        bool used = false;
        for (const auto& operand : instruction.operands) {
          if (values.contains(operand)) { used = true; break; }
        }
        if (!used) continue;
        use_blocks.push_back(block_index);
        region.uses.push_back({block.name, static_cast<std::uint32_t>(instruction_index)});
        // Stable deterministic location: later block order wins, then instruction.
        if (block_index > blocks_by_name[region.end_block] ||
            (block_index == blocks_by_name[region.end_block] && instruction_index > region.end_instruction)) {
          region.end_block = block.name;
          region.end_instruction = static_cast<std::uint32_t>(instruction_index);
        }
      }
    }

    // A region contains a loop backedge when the borrow can reach the
    // backedge source and the backedge target can reach one of the alias uses.
    // This includes loop headers/control blocks even when the alias itself is
    // only read in the body.
    const auto cfg_reaches = [&](std::size_t from, std::size_t to) {
      if (from == to) return true;
      std::vector<std::size_t> work{from};
      std::vector<bool> visited(function.blocks.size(), false);
      visited[from] = true;
      while (!work.empty()) {
        const auto current = work.back();
        work.pop_back();
        for (const auto successor : successors[current]) {
          if (successor == to) return true;
          if (!visited[successor]) {
            visited[successor] = true;
            work.push_back(successor);
          }
        }
      }
      return false;
    };
    for (std::size_t block_index = 0; block_index < successors.size(); ++block_index) {
      if (!cfg_reaches(binding.block, block_index)) continue;
      for (const auto successor : successors[block_index]) {
        if (successor > block_index) continue;
        for (const auto use_block : use_blocks) {
          if (cfg_reaches(successor, use_block)) {
            region.contains_back_edge = true;
            break;
          }
        }
        if (region.contains_back_edge) break;
      }
      if (region.contains_back_edge) break;
    }

    for (const auto& suspension : function.suspension_points) {
      for (const auto& value : suspension.live_values) {
        if (values.contains(value)) { region.crosses_suspension = true; break; }
      }
      if (region.crosses_suspension) break;
    }
  }

  // Reborrow constraints: if a child borrows through another reference local,
  // the parent region must include every child use. Propagate the child's end
  // point into the parent until the region graph reaches a fixed point.
  std::unordered_map<std::string, std::size_t> aliases;
  for (std::size_t index = 0; index < bindings.size(); ++index) {
    if (!bindings[index].region.alias_path.empty()) aliases[bindings[index].region.alias_path] = index;
  }

  for (std::size_t index = 0; index < bindings.size(); ++index) {
    auto root = bindings[index].region.source_path;
    const auto boundary = root.find_first_of(".[");
    if (boundary != std::string::npos) root.resize(boundary);
    if (const auto parent = aliases.find(root); parent != aliases.end() && parent->second != index) {
      bindings[index].region.parent_region = static_cast<std::int32_t>(parent->second);
    }
  }
  changed = true;
  while (changed) {
    changed = false;
    for (auto& binding : bindings) {
      if (binding.region.parent_region < 0) continue;
      auto& parent = bindings[static_cast<std::size_t>(binding.region.parent_region)].region;
      const auto child_block = blocks_by_name[binding.region.end_block];
      const auto parent_block = blocks_by_name[parent.end_block];
      if (child_block > parent_block ||
          (child_block == parent_block && binding.region.end_instruction > parent.end_instruction)) {
        parent.end_block = binding.region.end_block;
        parent.end_instruction = binding.region.end_instruction;
        changed = true;
      }
      parent.contains_back_edge = parent.contains_back_edge || binding.region.contains_back_edge;
      parent.crosses_suspension = parent.crosses_suspension || binding.region.crosses_suspension;
    }
  }

  for (auto& binding : bindings) function.borrow_regions.push_back(std::move(binding.region));
}

bool mir_is_ownership_metadata(MirOpcode opcode) noexcept {
  return opcode == MirOpcode::storage_live || opcode == MirOpcode::storage_dead ||
         opcode == MirOpcode::move_value || opcode == MirOpcode::borrow_shared ||
         opcode == MirOpcode::borrow_exclusive || opcode == MirOpcode::borrow_bind ||
         opcode == MirOpcode::place_path;
}

bool mir_is_terminator(MirOpcode opcode) noexcept {
  return opcode == MirOpcode::jump || opcode == MirOpcode::branch ||
         opcode == MirOpcode::return_value || opcode == MirOpcode::return_void ||
         opcode == MirOpcode::unreachable;
}

std::vector<std::string> MirModule::verify() const {
  std::vector<std::string> errors;
  std::unordered_set<std::uint64_t> dispatch_ids;
  std::unordered_set<std::string> dispatch_signatures;
  for (const auto& abi : dispatch_abis) {
    if (abi.canonical_signature.empty() || abi.signature_id == 0) errors.emplace_back("invalid dispatch ABI descriptor");
    if (!dispatch_ids.insert(abi.signature_id).second) errors.emplace_back("duplicate dispatch ABI signature id");
    if (!dispatch_signatures.insert(abi.canonical_signature).second) errors.emplace_back("duplicate dispatch ABI canonical signature");
    std::uint64_t previous_end = 0;
    for (const auto& field : abi.arguments) {
      if (field.alignment == 0 || field.size == 0 || field.offset % field.alignment != 0 || field.offset < previous_end)
        errors.emplace_back("invalid dispatch ABI argument layout for '" + abi.canonical_signature + "'");
      previous_end = field.offset + field.size;
    }
    if (abi.argument_alignment == 0 || abi.argument_size % abi.argument_alignment != 0 || abi.argument_size < previous_end)
      errors.emplace_back("invalid dispatch ABI frame size for '" + abi.canonical_signature + "'");
    if (abi.result_type == "void") {
      if (abi.result_size != 0) errors.emplace_back("void dispatch ABI result has storage");
    } else if (abi.result_size == 0 || abi.result_alignment == 0) {
      errors.emplace_back("invalid dispatch ABI result layout for '" + abi.canonical_signature + "'");
    }
  }
  std::unordered_map<std::string, const MirFunction*> functions_by_name;
  for (const auto& function : functions) {
    if (function.name.empty()) errors.emplace_back("MIR function has no name");
    if (!functions_by_name.emplace(function.name, &function).second) {
      errors.emplace_back("duplicate MIR function '" + function.name + "'");
    }
  }

  for (const auto& function : functions) {
    if (function.is_external) {
      if (!function.blocks.empty()) errors.emplace_back("external MIR function '" + function.name + "' must not have blocks");
      continue;
    }
    if (function.blocks.empty()) {
      errors.emplace_back("MIR function '" + function.name + "' has no blocks");
      continue;
    }
    std::unordered_set<std::string> block_names;
    std::unordered_set<std::string> values;
    if (!function.is_async && (!function.async_frame.empty() || !function.suspension_points.empty() || !function.async_state_machine.states.empty())) {
      errors.emplace_back("non-async MIR function '" + function.name + "' has async frame metadata");
    }
    std::uint32_t expected_state = 1;
    for (const auto& point : function.suspension_points) {
      if (point.state != expected_state++) errors.emplace_back("non-contiguous async state numbering in '" + function.name + "'");
    }
    if (function.is_async) {
      const auto& machine = function.async_state_machine;
      if (machine.frame_type.empty() || machine.constructor.empty() || machine.poll.empty() || machine.destroy.empty()) {
        errors.emplace_back("async MIR function '" + function.name + "' has no synthesized state-machine entry points");
      }
      if (machine.generated_functions.size() != 3) {
        errors.emplace_back("async MIR function '" + function.name + "' has incomplete generated state-machine bodies");
      } else {
        if (machine.generated_functions[0].kind != MirAsyncGeneratedKind::constructor ||
            machine.generated_functions[1].kind != MirAsyncGeneratedKind::poll ||
            machine.generated_functions[2].kind != MirAsyncGeneratedKind::destroy) {
          errors.emplace_back("async MIR function '" + function.name + "' has invalid generated state-machine body ordering");
        }
        for (const auto& generated : machine.generated_functions) {
          if (generated.name.empty() || generated.blocks.empty()) {
            errors.emplace_back("async MIR function '" + function.name + "' has an empty generated state-machine body");
          }
        }
      }
      if (function.async_regions.size() != function.suspension_points.size() + 1) {
        errors.emplace_back("async MIR function '" + function.name + "' has an invalid control-flow region plan");
      }
      for (std::size_t region_index = 0; region_index < function.async_regions.size(); ++region_index) {
        const auto& region = function.async_regions[region_index];
        if (region.state != region_index || region.entry_block.empty() || region.reachable_blocks.empty()) {
          errors.emplace_back("async MIR function '" + function.name + "' has a malformed control-flow region");
        }
      }
      for (const auto& slot : function.async_frame) {
        if (slot.owned && slot.cleanup_symbol.empty()) {
          errors.emplace_back("owned async frame slot has no cleanup symbol in '" + function.name + "'");
        }
        if (!slot.owned && !slot.cleanup_symbol.empty()) {
          errors.emplace_back("unowned async frame slot has cleanup symbol in '" + function.name + "'");
        }
        if (slot.projection_count != 0 && (!slot.owned || !slot.owned_bytes)) {
          errors.emplace_back("projected async frame slot is not owned byte storage in '" + function.name + "'");
        }
      }

      const auto parse_u64 = [](const std::string& text, std::uint64_t& value) {
        if (text.empty()) return false;
        const auto* first = text.data();
        const auto* last = first + text.size();
        const auto result = std::from_chars(first, last, value);
        return result.ec == std::errc{} && result.ptr == last;
      };
      const auto projected_slot = [&](const std::string& text) -> const MirAsyncFrameSlot* {
        std::uint64_t index = 0;
        if (!parse_u64(text, index) || index > std::numeric_limits<std::uint32_t>::max()) return nullptr;
        for (const auto& slot : function.async_frame) {
          if (slot.slot == index && slot.projection_count != 0) return &slot;
        }
        return nullptr;
      };
      const auto verify_projection_instruction = [&](const MirInstruction& instruction,
                                                     const std::string& generated_name) {
        const auto invalid = [&](const std::string& detail) {
          errors.emplace_back("invalid async projection operation in '" + function.name + "::" +
                              generated_name + "': " + detail);
        };
        if (instruction.opcode == MirOpcode::async_slot_projection_set) {
          if (!instruction.result.empty() || instruction.type_name != "bool" || instruction.operands.size() != 4) {
            invalid("malformed scalar update");
            return;
          }
          const auto* slot = projected_slot(instruction.operands[1]);
          std::uint64_t projection = 0;
          if (slot == nullptr || !parse_u64(instruction.operands[2], projection) || projection >= slot->projection_count)
            invalid("scalar update is outside its projected slot");
        } else if (instruction.opcode == MirOpcode::async_slot_projection_range_set) {
          if (!instruction.result.empty() || instruction.type_name != "bool" || instruction.operands.size() != 5) {
            invalid("malformed range update");
            return;
          }
          const auto* slot = projected_slot(instruction.operands[1]);
          std::uint64_t first = 0;
          std::uint64_t count = 0;
          if (slot == nullptr || !parse_u64(instruction.operands[2], first) ||
              !parse_u64(instruction.operands[3], count) || count == 0 || first >= slot->projection_count ||
              count > slot->projection_count - first) invalid("range update is outside its projected slot");
        } else if (instruction.opcode == MirOpcode::async_slot_projection_batch_set) {
          if (instruction.result.empty() || instruction.type_name != "i64" || instruction.operands.size() < 3) {
            invalid("malformed batch update");
            return;
          }
          const auto* slot = projected_slot(instruction.operands[1]);
          std::uint64_t run_count = 0;
          if (slot == nullptr || !parse_u64(instruction.operands[2], run_count) || run_count < 2 ||
              run_count > (std::numeric_limits<std::size_t>::max() - 3) / 3 ||
              instruction.operands.size() != 3 + static_cast<std::size_t>(run_count) * 3) {
            invalid("batch header does not match its transitions");
            return;
          }
          std::uint64_t previous_end = 0;
          for (std::uint64_t run = 0; run < run_count; ++run) {
            const auto base = 3 + static_cast<std::size_t>(run) * 3;
            std::uint64_t first = 0;
            std::uint64_t count = 0;
            if (!parse_u64(instruction.operands[base], first) ||
                !parse_u64(instruction.operands[base + 1], count) || count == 0 ||
                first < previous_end || first >= slot->projection_count ||
                count > slot->projection_count - first) {
              invalid("batch transitions are unsorted, overlapping, or out of range");
              return;
            }
            previous_end = first + count;
          }
        } else if (instruction.opcode == MirOpcode::async_slot_projection_mask) {
          if (instruction.result.empty() || instruction.type_name != "u64" || instruction.operands.size() != 2 ||
              projected_slot(instruction.operands[1]) == nullptr) invalid("malformed projection mask query");
        }
      };
      for (const auto& generated : machine.generated_functions) {
        struct GeneratedLocation final {
          std::size_t block = 0;
          std::size_t instruction = 0;
        };
        std::unordered_map<std::string, std::string> generated_value_types;
        std::unordered_map<std::string, GeneratedLocation> generated_value_locations;
        std::unordered_map<std::uint32_t, std::size_t> projected_allocations;
        std::unordered_map<std::uint32_t, GeneratedLocation> projected_allocation_locations;
        std::unordered_map<std::string, std::size_t> generated_block_indices;
        for (std::size_t block_index = 0; block_index < generated.blocks.size(); ++block_index) {
          generated_block_indices.emplace(generated.blocks[block_index].name, block_index);
        }
        std::vector<std::vector<std::size_t>> predecessors(generated.blocks.size());
        std::vector<std::vector<std::size_t>> successors(generated.blocks.size());
        for (std::size_t block_index = 0; block_index < generated.blocks.size(); ++block_index) {
          const auto& block = generated.blocks[block_index];
          if (block.instructions.empty()) continue;
          const auto& terminator = block.instructions.back();
          const auto add_edge = [&](const std::string& target) {
            const auto found = generated_block_indices.find(target);
            if (found == generated_block_indices.end()) return;
            predecessors[found->second].push_back(block_index);
            successors[block_index].push_back(found->second);
          };
          if (terminator.opcode == MirOpcode::jump && terminator.operands.size() == 1) {
            add_edge(terminator.operands[0]);
          } else if (terminator.opcode == MirOpcode::branch && terminator.operands.size() == 3) {
            add_edge(terminator.operands[1]);
            add_edge(terminator.operands[2]);
          } else if (terminator.opcode == MirOpcode::async_dispatch) {
            for (const auto& operand : terminator.operands) add_edge(operand);
          }
        }
        std::vector<bool> cyclic_blocks(generated.blocks.size(), false);
        for (std::size_t origin = 0; origin < generated.blocks.size(); ++origin) {
          std::vector<bool> visited(generated.blocks.size(), false);
          std::vector<std::size_t> pending(successors[origin].begin(), successors[origin].end());
          while (!pending.empty() && !cyclic_blocks[origin]) {
            const auto candidate = pending.back();
            pending.pop_back();
            if (candidate == origin) {
              cyclic_blocks[origin] = true;
              break;
            }
            if (candidate >= visited.size() || visited[candidate]) continue;
            visited[candidate] = true;
            pending.insert(pending.end(), successors[candidate].begin(), successors[candidate].end());
          }
        }
        std::vector<std::unordered_set<std::size_t>> dominators(generated.blocks.size());
        for (std::size_t block_index = 0; block_index < generated.blocks.size(); ++block_index) {
          if (block_index == 0) {
            dominators[block_index].insert(block_index);
          } else {
            for (std::size_t candidate = 0; candidate < generated.blocks.size(); ++candidate) {
              dominators[block_index].insert(candidate);
            }
          }
        }
        bool changed_dominators = true;
        while (changed_dominators) {
          changed_dominators = false;
          for (std::size_t block_index = 1; block_index < generated.blocks.size(); ++block_index) {
            std::unordered_set<std::size_t> next;
            if (!predecessors[block_index].empty()) {
              next = dominators[predecessors[block_index].front()];
              for (std::size_t predecessor_index = 1;
                   predecessor_index < predecessors[block_index].size(); ++predecessor_index) {
                const auto& predecessor_dominators = dominators[predecessors[block_index][predecessor_index]];
                for (auto candidate = next.begin(); candidate != next.end();) {
                  if (!predecessor_dominators.contains(*candidate)) candidate = next.erase(candidate);
                  else ++candidate;
                }
              }
            } else {
              next.clear();
            }
            next.insert(block_index);
            if (next != dominators[block_index]) {
              dominators[block_index] = std::move(next);
              changed_dominators = true;
            }
          }
        }
        const auto location_dominates = [&](const GeneratedLocation& definition,
                                            const GeneratedLocation& use) {
          if (definition.block == use.block) return definition.instruction < use.instruction;
          return use.block < dominators.size() && dominators[use.block].contains(definition.block);
        };
        for (const auto& [type, parameter] : generated.parameters) generated_value_types.emplace(parameter, type);
        for (std::size_t block_index = 0; block_index < generated.blocks.size(); ++block_index) {
          const auto& block = generated.blocks[block_index];
          for (std::size_t instruction_index = 0; instruction_index < block.instructions.size(); ++instruction_index) {
            const auto& instruction = block.instructions[instruction_index];
            if (!instruction.result.empty()) {
              generated_value_types.emplace(instruction.result, instruction.type_name);
              generated_value_locations.emplace(instruction.result, GeneratedLocation{block_index, instruction_index});
            }
            if (instruction.opcode != MirOpcode::async_slot_allocate_projected_bytes) continue;
            const auto invalid_allocation = [&](const std::string& detail) {
              errors.emplace_back("invalid projected async slot allocation in '" + function.name + "::" +
                                  generated.name + "': " + detail);
            };
            if (generated.kind != MirAsyncGeneratedKind::poll || instruction.result.empty() ||
                instruction.operands.size() != 7 || instruction.operands[0] != "frame") {
              invalid_allocation("malformed allocation or allocation outside the poll body");
              continue;
            }
            std::uint64_t slot_index = 0;
            std::uint64_t storage_size = 0;
            std::uint64_t storage_alignment = 0;
            std::uint64_t projection_count = 0;
            if (!parse_u64(instruction.operands[1], slot_index) ||
                !parse_u64(instruction.operands[2], storage_size) ||
                !parse_u64(instruction.operands[3], storage_alignment) ||
                !parse_u64(instruction.operands[6], projection_count) ||
                slot_index > std::numeric_limits<std::uint32_t>::max()) {
              invalid_allocation("allocation metadata is not numeric");
              continue;
            }
            const auto* slot = projected_slot(instruction.operands[1]);
            if (slot == nullptr || storage_size != slot->storage_size ||
                storage_alignment != slot->storage_alignment || projection_count != slot->projection_count ||
                instruction.operands[4] != slot->cleanup_symbol) {
              invalid_allocation("allocation metadata does not match the async frame slot");
              continue;
            }
            const auto normalized_slot = static_cast<std::uint32_t>(slot_index);
            if (block_index < cyclic_blocks.size() && cyclic_blocks[block_index]) {
              invalid_allocation("projected frame slot allocation is inside a control-flow cycle");
            }
            if (++projected_allocations[normalized_slot] != 1) {
              invalid_allocation("projected frame slot is allocated more than once");
            } else {
              projected_allocation_locations.emplace(normalized_slot,
                                                     GeneratedLocation{block_index, instruction_index});
            }
          }
        }
        for (std::size_t block_index = 0; block_index < generated.blocks.size(); ++block_index) {
          const auto& block = generated.blocks[block_index];
          for (std::size_t instruction_index = 0; instruction_index < block.instructions.size(); ++instruction_index) {
            const auto& instruction = block.instructions[instruction_index];
            verify_projection_instruction(instruction, generated.name);
            if (instruction.opcode == MirOpcode::async_slot_disarm ||
                instruction.opcode == MirOpcode::async_slot_take ||
                instruction.opcode == MirOpcode::async_slot_load ||
                instruction.opcode == MirOpcode::async_slot_transfer_projected) {
              const auto invalid_transfer = [&](const std::string& detail) {
                errors.emplace_back("invalid async projected-slot transfer in '" + function.name + "::" +
                                    generated.name + "': " + detail);
              };
              if (instruction.operands.size() == 2 && instruction.operands[0] == "frame") {
                const auto* slot = projected_slot(instruction.operands[1]);
                if (slot != nullptr) {
                  if (generated.kind != MirAsyncGeneratedKind::poll) {
                    invalid_transfer("projected slots may only be transferred inside the poll body");
                  }
                  std::uint64_t slot_index = 0;
                  const auto parsed_slot = parse_u64(instruction.operands[1], slot_index) &&
                                           slot_index <= std::numeric_limits<std::uint32_t>::max();
                  const auto normalized_slot = static_cast<std::uint32_t>(slot_index);
                  const auto allocation = parsed_slot ? projected_allocation_locations.find(normalized_slot)
                                                      : projected_allocation_locations.end();
                  if (!parsed_slot || allocation == projected_allocation_locations.end()) {
                    invalid_transfer("projected-slot transfer has no unique matching allocation");
                  }
                  if (instruction.opcode == MirOpcode::async_slot_take) {
                    invalid_transfer("projected byte storage must use atomic projected transfer, not take");
                  } else if (instruction.opcode == MirOpcode::async_slot_transfer_projected) {
                    if (instruction.result.empty()) invalid_transfer("projected transfer must produce the resumed value");
                  } else if (instruction.opcode == MirOpcode::async_slot_disarm) {
                    invalid_transfer("projected byte storage must use atomic projected transfer, not disarm");
                  } else {
                    invalid_transfer("projected byte storage must use atomic projected transfer, not load");
                  }
                }
              }
            }
            const bool is_update = instruction.opcode == MirOpcode::async_slot_projection_set ||
                                   instruction.opcode == MirOpcode::async_slot_projection_range_set ||
                                   instruction.opcode == MirOpcode::async_slot_projection_batch_set;
            if (!is_update) continue;
            const auto invalid_update = [&](const std::string& detail) {
              errors.emplace_back("invalid async projection state in '" + function.name + "::" +
                                  generated.name + "': " + detail);
            };
            if (generated.kind != MirAsyncGeneratedKind::poll || instruction.operands.empty() ||
                instruction.operands[0] != "frame") {
              invalid_update("projection updates must target the poll frame");
              continue;
            }
            std::uint64_t slot_index = 0;
            if (!parse_u64(instruction.operands[1], slot_index) ||
                slot_index > std::numeric_limits<std::uint32_t>::max() ||
                projected_allocations[static_cast<std::uint32_t>(slot_index)] != 1) {
              invalid_update("projection update has no unique matching slot allocation");
            } else {
              const auto normalized_slot = static_cast<std::uint32_t>(slot_index);
              const auto allocation = projected_allocation_locations.find(normalized_slot);
              const GeneratedLocation use{block_index, instruction_index};
              if (allocation == projected_allocation_locations.end() ||
                  !location_dominates(allocation->second, use)) {
                invalid_update("projection update is not dominated by its slot allocation");
              }
            }
            const GeneratedLocation use{block_index, instruction_index};
            const auto verify_state = [&](const std::string& value) {
              const auto found = generated_value_types.find(value);
              if (found == generated_value_types.end() || found->second != "bool") {
                invalid_update("projection state operand is not a generated bool value");
                return;
              }
              const auto definition = generated_value_locations.find(value);
              if (definition != generated_value_locations.end() &&
                  !location_dominates(definition->second, use)) {
                invalid_update("projection state operand does not dominate its update");
              }
            };
            if (instruction.opcode == MirOpcode::async_slot_projection_set) {
              if (instruction.operands.size() == 4) verify_state(instruction.operands[3]);
            } else if (instruction.opcode == MirOpcode::async_slot_projection_range_set) {
              if (instruction.operands.size() == 5) verify_state(instruction.operands[4]);
            } else if (instruction.operands.size() >= 3) {
              std::uint64_t run_count = 0;
              if (parse_u64(instruction.operands[2], run_count) &&
                  run_count <= (instruction.operands.size() - 3) / 3) {
                for (std::uint64_t run = 0; run < run_count; ++run) {
                  verify_state(instruction.operands[5 + static_cast<std::size_t>(run) * 3]);
                }
              }
            }
          }
        }
        // A projected aggregate transfer consumes the frame slot's cleanup ownership.
        // No later operation on that slot may be reachable from the paired load: doing so
        // would permit a second transfer, republish stale drop state, or otherwise use a
        // slot whose cleanup has already been disarmed.
        const auto projected_slot_operand = [&](const MirInstruction& instruction) -> std::optional<std::uint32_t> {
          const bool relevant = instruction.opcode == MirOpcode::async_slot_allocate_projected_bytes ||
                                instruction.opcode == MirOpcode::async_slot_disarm ||
                                instruction.opcode == MirOpcode::async_slot_transfer_projected ||
                                instruction.opcode == MirOpcode::async_slot_take ||
                                instruction.opcode == MirOpcode::async_slot_load ||
                                instruction.opcode == MirOpcode::async_slot_projection_set ||
                                instruction.opcode == MirOpcode::async_slot_projection_range_set ||
                                instruction.opcode == MirOpcode::async_slot_projection_batch_set;
          if (!relevant || instruction.operands.size() < 2 || instruction.operands[0] != "frame") {
            return std::nullopt;
          }
          std::uint64_t slot_index = 0;
          if (!parse_u64(instruction.operands[1], slot_index) ||
              slot_index > std::numeric_limits<std::uint32_t>::max() ||
              projected_slot(instruction.operands[1]) == nullptr) {
            return std::nullopt;
          }
          return static_cast<std::uint32_t>(slot_index);
        };
        for (std::size_t block_index = 0; block_index < generated.blocks.size(); ++block_index) {
          const auto& block = generated.blocks[block_index];
          for (std::size_t instruction_index = 0; instruction_index < block.instructions.size(); ++instruction_index) {
            const auto& instruction = block.instructions[instruction_index];
            if (instruction.opcode != MirOpcode::async_slot_transfer_projected) continue;
            const auto transferred_slot = projected_slot_operand(instruction);
            if (!transferred_slot.has_value()) continue;
            bool reported_use_after_transfer = false;
            const auto report_use_after_transfer = [&]() {
              if (reported_use_after_transfer) return;
              errors.emplace_back("invalid async projected-slot transfer in '" + function.name + "::" +
                                  generated.name + "': projected slot is used after ownership transfer");
              reported_use_after_transfer = true;
            };
            for (std::size_t later = instruction_index + 1; later < block.instructions.size(); ++later) {
              if (projected_slot_operand(block.instructions[later]) == transferred_slot) {
                report_use_after_transfer();
                break;
              }
            }
            std::vector<bool> visited(generated.blocks.size(), false);
            std::vector<std::size_t> pending(successors[block_index].begin(), successors[block_index].end());
            while (!pending.empty() && !reported_use_after_transfer) {
              const auto candidate = pending.back();
              pending.pop_back();
              if (candidate >= generated.blocks.size() || visited[candidate]) continue;
              visited[candidate] = true;
              for (const auto& candidate_instruction : generated.blocks[candidate].instructions) {
                if (projected_slot_operand(candidate_instruction) == transferred_slot) {
                  report_use_after_transfer();
                  break;
                }
              }
              if (!reported_use_after_transfer) {
                pending.insert(pending.end(), successors[candidate].begin(), successors[candidate].end());
              }
            }
          }
        }
      }
      if (machine.states.size() != function.suspension_points.size() + 3) {
        errors.emplace_back("async MIR function '" + function.name + "' has an invalid synthesized state count");
      } else {
        if (machine.states.front().state != 0 || machine.states.front().kind != MirAsyncStateKind::entry) {
          errors.emplace_back("async MIR function '" + function.name + "' has no entry state");
        }
        if (machine.states[machine.states.size() - 2].kind != MirAsyncStateKind::completed ||
            machine.states.back().kind != MirAsyncStateKind::cancelled) {
          errors.emplace_back("async MIR function '" + function.name + "' has invalid terminal states");
        }
      }
    }
    for (const auto& [_, parameter] : function.parameters) values.insert(parameter);
    for (const auto& block : function.blocks) {
      if (block.name.empty()) errors.emplace_back("MIR block in '" + function.name + "' has no name");
      if (!block_names.insert(block.name).second) errors.emplace_back("duplicate MIR block '" + function.name + "::" + block.name + "'");
      for (const auto& instruction : block.instructions) {
        if (!instruction.result.empty() && !values.insert(instruction.result).second) {
          errors.emplace_back("duplicate MIR value '" + instruction.result + "'");
        }
      }
    }

    for (const auto& block : function.blocks) {
      bool terminated = false;
      for (const auto& instruction : block.instructions) {
        if (terminated) errors.emplace_back("instruction appears after terminator in '" + function.name + "::" + block.name + "'");
        if (instruction.opcode == MirOpcode::jump) {
          if (instruction.operands.size() != 1 || !block_names.contains(instruction.operands[0])) {
            errors.emplace_back("invalid jump target in '" + function.name + "::" + block.name + "'");
          }
        } else if (instruction.opcode == MirOpcode::branch) {
          if (instruction.operands.size() != 3 || !block_names.contains(instruction.operands[1]) ||
              !block_names.contains(instruction.operands[2])) {
            errors.emplace_back("invalid branch in '" + function.name + "::" + block.name + "'");
          }
        } else if (instruction.opcode == MirOpcode::stack_allocate) {
          if (instruction.result.empty() || instruction.operands.size() != 2) {
            errors.emplace_back("invalid stack allocation in '" + function.name + "::" + block.name + "'");
          }
        } else if (instruction.opcode == MirOpcode::load) {
          if (instruction.result.empty() || instruction.operands.size() != 1) {
            errors.emplace_back("invalid load in '" + function.name + "::" + block.name + "'");
          }
        } else if (instruction.opcode == MirOpcode::store) {
          if (!instruction.result.empty() || instruction.operands.size() != 2) {
            errors.emplace_back("invalid store in '" + function.name + "::" + block.name + "'");
          }
        } else if (instruction.opcode == MirOpcode::pointer_offset) {
          if (instruction.result.empty() || instruction.operands.size() != 2) {
            errors.emplace_back("invalid pointer offset in '" + function.name + "::" + block.name + "'");
          }
        } else if (instruction.opcode == MirOpcode::drop) {
          if (!instruction.result.empty() || instruction.type_name.empty() || instruction.operands.size() != 1) {
            errors.emplace_back("invalid drop in '" + function.name + "::" + block.name + "'");
          }
        } else if (instruction.opcode == MirOpcode::storage_live ||
                   instruction.opcode == MirOpcode::storage_dead) {
          if (!instruction.result.empty() || instruction.type_name.empty() || instruction.operands.size() != 1) {
            errors.emplace_back("invalid storage lifetime marker in '" + function.name + "::" + block.name + "'");
          }
        } else if (instruction.opcode == MirOpcode::move_value) {
          if (!instruction.result.empty() || instruction.operands.empty() || instruction.operands.size() > 2) {
            errors.emplace_back("invalid move ownership marker in '" + function.name + "::" + block.name + "'");
          }
        } else if (instruction.opcode == MirOpcode::borrow_shared ||
                   instruction.opcode == MirOpcode::borrow_exclusive) {
          if (!instruction.result.empty() || instruction.type_name.empty() ||
              instruction.operands.empty() || instruction.operands.size() > 2) {
            errors.emplace_back("invalid borrow ownership marker in '" + function.name + "::" + block.name + "'");
          }
        } else if (instruction.opcode == MirOpcode::borrow_bind) {
          if (!instruction.result.empty() || instruction.type_name.empty() || instruction.operands.size() < 2 ||
              instruction.operands.size() > 4 || instruction.operands[0].empty() || instruction.operands[1].empty()) {
            errors.emplace_back("invalid borrow region binding in '" + function.name + "::" + block.name + "'");
          }
        } else if (instruction.opcode == MirOpcode::place_path) {
          if (!instruction.result.empty() || instruction.operands.size() != 2 ||
              instruction.operands[0].empty() || instruction.operands[1].empty()) {
            errors.emplace_back("invalid projection ownership marker in '" + function.name + "::" + block.name + "'");
          }
        } else if (instruction.opcode == MirOpcode::async_result_load || instruction.opcode == MirOpcode::async_cancel_requested) {
          if (instruction.result.empty() || instruction.operands.size() != 1) {
            errors.emplace_back("invalid async frame query in '" + function.name + "::" + block.name + "'");
          }
        } else if (instruction.opcode == MirOpcode::async_result_store) {
          if (!instruction.result.empty() || instruction.operands.size() != 2) {
            errors.emplace_back("invalid async result store in '" + function.name + "::" + block.name + "'");
          }
        } else if (instruction.opcode == MirOpcode::async_frame_cancel) {
          if (!instruction.result.empty() || instruction.operands.size() != 1) {
            errors.emplace_back("invalid async frame cancellation in '" + function.name + "::" + block.name + "'");
          }
        } else if (instruction.opcode == MirOpcode::async_await_poll) {
          if (instruction.result.empty() || instruction.operands.size() != 3) {
            errors.emplace_back("invalid async await poll in '" + function.name + "::" + block.name + "'");
          }
        } else if (instruction.opcode == MirOpcode::async_await_result) {
          if (instruction.result.empty() || instruction.operands.size() != 1) {
            errors.emplace_back("invalid async await result in '" + function.name + "::" + block.name + "'");
          }
        } else if (instruction.opcode == MirOpcode::async_await || instruction.opcode == MirOpcode::task_spawn) {
          if (instruction.result.empty() || instruction.operands.size() != 1) {
            errors.emplace_back("invalid async operation in '" + function.name + "::" + block.name + "'");
          }
          if (!function.is_async) {
            errors.emplace_back("async operation appears in non-async MIR function '" + function.name + "'");
          }
        } else if (instruction.opcode == MirOpcode::call) {
          if (instruction.operands.empty()) {
            errors.emplace_back("call has no callee in '" + function.name + "::" + block.name + "'");
          } else if (!functions_by_name.contains(instruction.operands.front()) && instruction.operands.front() != "print") {
            errors.emplace_back("call references unknown function '" + instruction.operands.front() + "'");
          }
        } else if (instruction.opcode == MirOpcode::function_address) {
          if (instruction.result.empty() || instruction.operands.size() != 1 ||
              !functions_by_name.contains(instruction.operands.front())) {
            errors.emplace_back("invalid function address in '" + function.name + "::" + block.name + "'");
          }
        } else if (instruction.opcode == MirOpcode::call_indirect) {
          if (instruction.result.empty() && instruction.type_name != "void") {
            errors.emplace_back("value-returning indirect call has no result in '" + function.name + "::" + block.name + "'");
          }
          if (instruction.operands.size() < 3 || !functions_by_name.contains(instruction.operands[1])) {
            errors.emplace_back("indirect call has no valid signature in '" + function.name + "::" + block.name + "'");
          }
        } else if (instruction.opcode == MirOpcode::callable_create) {
          if (instruction.result.empty() || instruction.operands.size() < 3 ||
              !functions_by_name.contains(instruction.operands.front())) {
            errors.emplace_back("invalid callable creation in '" + function.name + "::" + block.name + "'");
          }
        } else if (instruction.opcode == MirOpcode::callable_clone) {
          if (instruction.result.empty() || instruction.operands.size() != 1) {
            errors.emplace_back("invalid callable clone in '" + function.name + "::" + block.name + "'");
          }
        } else if (instruction.opcode == MirOpcode::callable_invoke) {
          if (instruction.operands.empty() || (instruction.result.empty() && instruction.type_name != "void")) {
            errors.emplace_back("invalid callable invocation in '" + function.name + "::" + block.name + "'");
          }
        } else if (instruction.opcode == MirOpcode::callable_destroy) {
          if (!instruction.result.empty() || instruction.operands.size() != 1) {
            errors.emplace_back("invalid callable destruction in '" + function.name + "::" + block.name + "'");
          }
        }
        else if (instruction.opcode == MirOpcode::trait_object_create) {
          if (instruction.result.empty() || instruction.operands.size() < 2) {
            errors.emplace_back("invalid trait object creation in '" + function.name + "::" + block.name + "'");
          } else {
            for (std::size_t index = 3; index < instruction.operands.size(); ++index) {
              if (!functions_by_name.contains(instruction.operands[index])) {
                errors.emplace_back("trait object vtable references unknown function '" + instruction.operands[index] + "'");
              }
            }
          }
        } else if (instruction.opcode == MirOpcode::trait_object_clone) {
          if (instruction.result.empty() || instruction.operands.size() != 1) {
            errors.emplace_back("invalid trait object clone in '" + function.name + "::" + block.name + "'");
          }
        } else if (instruction.opcode == MirOpcode::trait_object_invoke) {
          if (instruction.operands.size() < 2 || (instruction.result.empty() && instruction.type_name != "void")) {
            errors.emplace_back("invalid trait object invocation in '" + function.name + "::" + block.name + "'");
          }
        } else if (instruction.opcode == MirOpcode::trait_object_destroy) {
          if (!instruction.result.empty() || instruction.operands.size() != 1) {
            errors.emplace_back("invalid trait object destruction in '" + function.name + "::" + block.name + "'");
          }
        }
        if (mir_is_terminator(instruction.opcode)) terminated = true;
      }
      if (!terminated) errors.emplace_back("MIR block '" + function.name + "::" + block.name + "' has no terminator");
    }

    // Ownership-state verification is deliberately performed after structural
    // verification. Forge/LLVM may assume these invariants once MIR verifies.
    // State masks are joined across CFG edges, so a use is rejected if *any*
    // predecessor can reach it after a move or final drop.
    if (!function.blocks.empty()) {
      constexpr std::uint8_t state_live = 1u << 0;
      constexpr std::uint8_t state_dead = 1u << 1;
      constexpr std::uint8_t state_moved = 1u << 2;

      std::unordered_map<std::string, std::string> storage_root;
      for (const auto& block : function.blocks) {
        for (const auto& instruction : block.instructions) {
          if (instruction.opcode == MirOpcode::stack_allocate && !instruction.result.empty()) {
            storage_root[instruction.result] = instruction.result;
          }
        }
      }
      bool roots_changed = true;
      while (roots_changed) {
        roots_changed = false;
        for (const auto& block : function.blocks) {
          for (const auto& instruction : block.instructions) {
            if (instruction.opcode != MirOpcode::pointer_offset || instruction.result.empty() ||
                instruction.operands.empty() || storage_root.contains(instruction.result)) continue;
            if (const auto base = storage_root.find(instruction.operands.front()); base != storage_root.end()) {
              storage_root[instruction.result] = base->second;
              roots_changed = true;
            }
          }
        }
      }

      std::unordered_map<std::string, std::size_t> ownership_block_index;
      for (std::size_t index = 0; index < function.blocks.size(); ++index)
        ownership_block_index.emplace(function.blocks[index].name, index);
      std::vector<std::vector<std::size_t>> ownership_successors(function.blocks.size());
      for (std::size_t index = 0; index < function.blocks.size(); ++index) {
        const auto& instructions = function.blocks[index].instructions;
        if (instructions.empty()) continue;
        const auto& terminator = instructions.back();
        const auto add_successor = [&](const std::string& target) {
          if (const auto found = ownership_block_index.find(target); found != ownership_block_index.end())
            ownership_successors[index].push_back(found->second);
        };
        if (terminator.opcode == MirOpcode::jump && !terminator.operands.empty())
          add_successor(terminator.operands[0]);
        if (terminator.opcode == MirOpcode::branch && terminator.operands.size() == 3) {
          add_successor(terminator.operands[1]);
          add_successor(terminator.operands[2]);
        }
      }

      std::unordered_map<std::string, const MirInstruction*> ownership_definition;
      for (const auto& block : function.blocks) {
        for (const auto& instruction : block.instructions) {
          if (!instruction.result.empty()) ownership_definition[instruction.result] = &instruction;
        }
      }

      std::unordered_map<std::string, std::string> ownership_path;
      for (const auto& block : function.blocks) {
        for (const auto& instruction : block.instructions) {
          if (instruction.opcode == MirOpcode::place_path && instruction.operands.size() == 2)
            ownership_path[instruction.operands[0]] = instruction.operands[1];
        }
      }
      std::unordered_map<std::string, std::string> ownership_type;
      for (const auto& parameter : function.parameters) ownership_type[parameter.second] = parameter.first;
      for (const auto& block : function.blocks) {
        for (const auto& instruction : block.instructions) {
          if (!instruction.result.empty() && !instruction.type_name.empty())
            ownership_type[instruction.result] = instruction.type_name;
        }
      }
      const std::unordered_set<std::string> ownership_enum_types(enum_types.begin(), enum_types.end());

      std::unordered_set<std::string> ownership_enum_roots;
      for (const auto& block : function.blocks) {
        for (const auto& instruction : block.instructions) {
          if (instruction.operands.size() != 2 || instruction.opcode != MirOpcode::move_value) continue;
          const auto variant = instruction.operands[1].find(".variant$");
          if (variant != std::string::npos) ownership_enum_roots.insert(instruction.operands[1].substr(0, variant));
        }
      }

      // Older hand-built MIR may carry only the logical path on move/borrow
      // metadata. Infer the root binding so projection verification remains
      // backwards-compatible while compiler-generated MIR uses place.path.
      for (const auto& block : function.blocks) {
        for (const auto& instruction : block.instructions) {
          if (instruction.operands.size() != 2 ||
              (instruction.opcode != MirOpcode::move_value &&
               instruction.opcode != MirOpcode::borrow_shared &&
               instruction.opcode != MirOpcode::borrow_exclusive)) continue;
          const auto physical = storage_root.find(instruction.operands[0]);
          if (physical == storage_root.end() || ownership_path.contains(physical->second)) continue;
          const auto& logical = instruction.operands[1];
          const auto boundary = logical.find_first_of(".[");
          ownership_path[physical->second] = logical.substr(0, boundary);
        }
      }

      // Tagged-enum discriminants are a distinct projection. Infer .$tag for
      // offset-zero i32 projections even when lowering did not have a source-level
      // logical path (clone/copy/? machinery), so moving a payload does not poison
      // later discriminant reads.
      bool inferred_projection = true;
      while (inferred_projection) {
        inferred_projection = false;
        for (const auto& block : function.blocks) {
          for (const auto& instruction : block.instructions) {
            if (instruction.opcode != MirOpcode::pointer_offset || instruction.result.empty() ||
                instruction.operands.size() != 2 || instruction.operands[1] != "0" ||
                instruction.type_name != "i32" || ownership_path.contains(instruction.result)) continue;
            const auto base_type = ownership_type.find(instruction.operands[0]);
            std::string base_path;
            if (const auto direct = ownership_path.find(instruction.operands[0]); direct != ownership_path.end()) {
              base_path = direct->second;
            } else if (const auto root = storage_root.find(instruction.operands[0]); root != storage_root.end()) {
              if (const auto rooted = ownership_path.find(root->second); rooted != ownership_path.end())
                base_path = rooted->second;
            }
            if (base_path.empty()) continue;
            const auto root_boundary = base_path.find_first_of(".[");
            const auto logical_root = base_path.substr(0, root_boundary);
            const bool typed_enum = base_type != ownership_type.end() && ownership_enum_types.contains(base_type->second);
            if (!typed_enum && !ownership_enum_roots.contains(logical_root)) continue;
            ownership_path[instruction.result] = logical_root + ".$tag";
            inferred_projection = true;
          }
        }
      }

      // Compiler-generated aggregate copies/projected bookkeeping may not have a
      // source-level field name. Preserve the immediate base pointer's logical
      // ownership domain instead of falling back to the physical allocation root.
      bool inherited_path = true;
      while (inherited_path) {
        inherited_path = false;
        for (const auto& block : function.blocks) {
          for (const auto& instruction : block.instructions) {
            if (instruction.opcode != MirOpcode::pointer_offset || instruction.result.empty() ||
                instruction.operands.empty() || ownership_path.contains(instruction.result)) continue;
            if (const auto base = ownership_path.find(instruction.operands.front()); base != ownership_path.end()) {
              ownership_path[instruction.result] = base->second;
              inherited_path = true;
            }
          }
        }
      }

      const auto root_for = [&](const std::string& value) -> std::string {
        const auto found = storage_root.find(value);
        return found == storage_root.end() ? std::string{} : found->second;
      };
      const auto path_for = [&](const std::string& value) -> std::string {
        if (const auto path = ownership_path.find(value); path != ownership_path.end()) return path->second;
        const auto root = root_for(value);
        if (root.empty()) return {};
        if (const auto path = ownership_path.find(root); path != ownership_path.end()) return path->second;
        return root;
      };
      const auto paths_overlap = [](std::string_view left, std::string_view right) {
        if (left.empty() || right.empty()) return false;
        std::size_t l = 0;
        std::size_t r = 0;
        while (l < left.size() && r < right.size()) {
          if (left[l] == '[' && right[r] == '[') {
            const auto l_end = left.find(']', l + 1);
            const auto r_end = right.find(']', r + 1);
            if (l_end == std::string_view::npos || r_end == std::string_view::npos) return left == right;
            const auto l_index = left.substr(l + 1, l_end - l - 1);
            const auto r_index = right.substr(r + 1, r_end - r - 1);
            if (l_index != "*" && r_index != "*" && l_index != r_index) return false;
            l = l_end + 1;
            r = r_end + 1;
            continue;
          }
          if (left[l] != right[r]) return false;
          ++l;
          ++r;
        }
        if (l == left.size() && r == right.size()) return true;
        if (l == left.size()) return right[r] == '.' || right[r] == '[';
        if (r == right.size()) return left[l] == '.' || left[l] == '[';
        return false;
      };

      // Borrow regions are inferred from actual MIR uses. Verify that every
      // source storage/projection outlives all uses of the alias and that
      // reborrow regions remain nested inside their parent loan.
      if (!function.borrow_regions.empty()) {
        std::unordered_map<std::string, std::size_t> region_blocks;
        for (std::size_t index = 0; index < function.blocks.size(); ++index)
          region_blocks.emplace(function.blocks[index].name, index);

        const auto reaches = [&](std::size_t from, std::size_t to) {
          if (from == to) return true;
          std::vector<std::size_t> work{from};
          std::vector<bool> visited(function.blocks.size(), false);
          visited[from] = true;
          while (!work.empty()) {
            const auto current = work.back();
            work.pop_back();
            for (const auto successor : ownership_successors[current]) {
              if (successor == to) return true;
              if (!visited[successor]) {
                visited[successor] = true;
                work.push_back(successor);
              }
            }
          }
          return false;
        };

        for (std::size_t region_index = 0; region_index < function.borrow_regions.size(); ++region_index) {
          const auto& region = function.borrow_regions[region_index];
          if (region.id != region_index || region.alias_place.empty() || region.begin_block.empty() ||
              !region_blocks.contains(region.begin_block) || !region_blocks.contains(region.end_block)) {
            errors.emplace_back("malformed borrow region in '" + function.name + "'");
            continue;
          }
          if (region.parent_region >= static_cast<std::int32_t>(function.borrow_regions.size())) {
            errors.emplace_back("borrow region has an invalid parent in '" + function.name + "'");
          }

          // Find storage-ending operations for the source ownership path.
          if (!region.source_path.empty()) {
            for (std::size_t block_index = 0; block_index < function.blocks.size(); ++block_index) {
              const auto& candidate_block = function.blocks[block_index];
              for (std::size_t instruction_index = 0; instruction_index < candidate_block.instructions.size(); ++instruction_index) {
                const auto& instruction = candidate_block.instructions[instruction_index];
                std::string ended_path;
                if ((instruction.opcode == MirOpcode::storage_dead || instruction.opcode == MirOpcode::drop) &&
                    !instruction.operands.empty()) {
                  ended_path = path_for(instruction.operands[0]);
                } else if (instruction.opcode == MirOpcode::move_value && !instruction.operands.empty()) {
                  ended_path = instruction.operands.size() == 2 ? instruction.operands[1] : path_for(instruction.operands[0]);
                }
                if (ended_path.empty() || !paths_overlap(region.source_path, ended_path)) continue;
                for (const auto& use : region.uses) {
                  const auto use_block = region_blocks.find(use.block);
                  if (use_block == region_blocks.end()) continue;
                  const bool after_same_block = use_block->second == block_index && use.instruction > instruction_index;
                  const bool after_successor = use_block->second != block_index && reaches(block_index, use_block->second);
                  if (after_same_block || after_successor) {
                    errors.emplace_back("borrow region violation in '" + function.name + "': alias '" +
                                        region.alias_path + "' outlives borrowed projection '" + region.source_path + "'");
                    break;
                  }
                }
              }
            }
          }

          if (region.parent_region >= 0) {
            const auto& parent = function.borrow_regions[static_cast<std::size_t>(region.parent_region)];
            const auto parent_end_block = region_blocks.find(parent.end_block);
            const auto child_end_block = region_blocks.find(region.end_block);
            if (parent_end_block != region_blocks.end() && child_end_block != region_blocks.end()) {
              const bool parent_cannot_cover_child =
                  parent_end_block->second == child_end_block->second
                      ? parent.end_instruction < region.end_instruction
                      : !reaches(child_end_block->second, parent_end_block->second) &&
                        parent_end_block->second < child_end_block->second;
              if (parent_cannot_cover_child) {
                errors.emplace_back("reborrow region escapes its parent loan in '" + function.name + "'");
              }
            }
          }

          if (region.crosses_suspension) {
            bool source_in_frame = false;
            for (const auto& slot : function.async_frame) {
              if (slot.value == region.source_place) { source_in_frame = true; break; }
            }
            bool source_is_parameter = false;
            for (const auto& [_, parameter] : function.parameters) {
              if (parameter == region.source_place || parameter == region.source_path) {
                source_is_parameter = true;
                break;
              }
            }
            if (!source_in_frame && !source_is_parameter) {
              errors.emplace_back("borrow region crosses async suspension without stable source storage in '" +
                                  function.name + "': '" + region.alias_path + "'");
            }
          }
        }
      }

      using OwnershipState = std::unordered_map<std::string, std::uint8_t>;
      std::vector<OwnershipState> ownership_in(function.blocks.size());
      std::vector<bool> ownership_reached(function.blocks.size(), false);
      ownership_reached[0] = true;
      std::vector<std::size_t> pending{0};
      std::unordered_set<std::string> ownership_reports;
      const auto report_ownership = [&](const std::string& message) {
        if (ownership_reports.insert(message).second) errors.push_back(message);
      };
      const auto invalid_state = [&](std::uint8_t state) {
        return (state & (state_dead | state_moved)) != 0;
      };

      while (!pending.empty()) {
        const auto block_index = pending.back();
        pending.pop_back();
        auto state = ownership_in[block_index];
        std::unordered_set<std::string> pending_moves;
        const auto& block = function.blocks[block_index];
        const bool drop_flag_guarded = block.name.starts_with("drop.");

        const auto require_live = [&](const std::string& value, std::string_view operation, bool guarded = false) {
          // drop.* blocks are compiler-generated conditional cleanup regions.
          // Their drop flags/discriminants prove the selected projection is initialized
          // on the active edge. Ordinary MIR receives no such exemption.
          if (guarded) return;
          const auto path = path_for(value);
          if (path.empty()) return;
          for (const auto& [tracked, mask] : state) {
            if (!invalid_state(mask) || !paths_overlap(path, tracked)) continue;
            std::string producer;
            if (const auto defined = ownership_definition.find(value); defined != ownership_definition.end()) {
              producer = " produced by " + std::string(mir_opcode_name(defined->second->opcode));
              if (!defined->second->type_name.empty()) producer += "<" + defined->second->type_name + ">";
              if (!defined->second->operands.empty()) {
                producer += "(";
                for (std::size_t index = 0; index < defined->second->operands.size(); ++index) {
                  if (index != 0) producer += ",";
                  producer += defined->second->operands[index];
                }
                producer += ")";
                if (defined->second->opcode == MirOpcode::pointer_offset) {
                  const auto base = ownership_definition.find(defined->second->operands.front());
                  if (base != ownership_definition.end()) {
                    producer += " from " + std::string(mir_opcode_name(base->second->opcode));
                    if (!base->second->type_name.empty()) producer += "<" + base->second->type_name + ">";
                    producer += "(" + base->second->result + ")";
                  }
                }
              }
            }
            report_ownership("ownership violation in '" + function.name + "::" + block.name +
                             "': " + std::string(operation) + " of MIR value '" + value + producer +
                             "' uses projection '" + path + "' after move/drop of overlapping projection '" +
                             tracked + "' on at least one control-flow path");
          }
        };
        const auto reinitialize = [&](const std::string& value) {
          const auto path = path_for(value);
          if (path.empty()) return;
          for (auto iterator = state.begin(); iterator != state.end();) {
            const auto& tracked = iterator->first;
            const bool same_or_child = tracked == path ||
                (tracked.starts_with(path) && tracked.size() > path.size() &&
                 (tracked[path.size()] == '.' || tracked[path.size()] == '['));
            if (same_or_child) iterator = state.erase(iterator);
            else ++iterator;
          }
          state[path] = state_live;
          pending_moves.erase(path);
        };

        for (const auto& instruction : block.instructions) {
          if (instruction.opcode == MirOpcode::place_path || instruction.opcode == MirOpcode::borrow_bind) continue;
          if (instruction.opcode == MirOpcode::stack_allocate) {
            if (!instruction.result.empty()) {
              const auto path = path_for(instruction.result);
              if (!path.empty()) state[path] = state_live;
            }
            continue;
          }
          if (instruction.opcode == MirOpcode::storage_live) {
            if (!instruction.operands.empty()) reinitialize(instruction.operands[0]);
            continue;
          }
          if (instruction.opcode == MirOpcode::move_value) {
            if (instruction.operands.empty()) continue;
            require_live(instruction.operands[0], "move");
            auto path = instruction.operands.size() == 2 ? instruction.operands[1] : path_for(instruction.operands[0]);
            if (!path.empty()) pending_moves.insert(std::move(path));
            continue;
          }
          if (instruction.opcode == MirOpcode::drop) {
            if (instruction.operands.empty()) continue;
            require_live(instruction.operands[0], "drop", drop_flag_guarded);
            const auto path = path_for(instruction.operands[0]);
            if (!path.empty()) {
              pending_moves.erase(path);
              state[path] = state_dead;
            }
            continue;
          }
          if (instruction.opcode == MirOpcode::storage_dead) {
            if (instruction.operands.empty()) continue;
            const auto path = path_for(instruction.operands[0]);
            if (!path.empty()) state[path] = state_dead;
            continue;
          }
          if (instruction.opcode == MirOpcode::load) {
            if (!instruction.operands.empty()) require_live(instruction.operands[0], "load", drop_flag_guarded);
          } else if (instruction.opcode == MirOpcode::store) {
            if (instruction.operands.size() == 2) {
              require_live(instruction.operands[0], "store source");
              reinitialize(instruction.operands[1]);
            }
          } else if (instruction.opcode == MirOpcode::pointer_offset) {
            if (!instruction.operands.empty()) require_live(instruction.operands[0], "projection", drop_flag_guarded);
          } else if (instruction.opcode == MirOpcode::borrow_shared ||
                     instruction.opcode == MirOpcode::borrow_exclusive) {
            if (!instruction.operands.empty()) require_live(instruction.operands[0], "borrow", drop_flag_guarded);
          } else if (instruction.opcode == MirOpcode::call || instruction.opcode == MirOpcode::call_indirect ||
                     instruction.opcode == MirOpcode::callable_create || instruction.opcode == MirOpcode::callable_invoke ||
                     instruction.opcode == MirOpcode::trait_object_create || instruction.opcode == MirOpcode::trait_object_invoke) {
            for (const auto& operand : instruction.operands) require_live(operand, "call", drop_flag_guarded);
          } else if (instruction.opcode == MirOpcode::return_value && !instruction.operands.empty()) {
            require_live(instruction.operands[0], "return");
          }
        }

        for (const auto& path : pending_moves) state[path] = state_moved;

        for (const auto successor : ownership_successors[block_index]) {
          bool changed = !ownership_reached[successor];
          ownership_reached[successor] = true;
          auto& incoming = ownership_in[successor];
          for (const auto& [path, mask] : state) {
            const auto previous = incoming[path];
            incoming[path] = static_cast<std::uint8_t>(previous | mask);
            changed = changed || incoming[path] != previous;
          }
          if (changed) pending.push_back(successor);
        }
      }
    }
  }
  return errors;
}

void MirModule::dump(std::ostream& stream) const {
  stream << "mir.module\n";
  for (const auto& abi : dispatch_abis) {
    stream << "  abi.dispatch " << (abi.kind == MirDispatchAbiKind::callable ? "callable" : "trait.method")
           << " id=" << abi.signature_id << " args=" << abi.argument_size << '/' << abi.argument_alignment
           << " result=" << abi.result_size << '/' << abi.result_alignment << ' ' << abi.canonical_signature << "\n";
    for (const auto& field : abi.arguments)
      stream << "    field " << field.type_name << " @" << field.offset << " size=" << field.size << " align=" << field.alignment << "\n";
  }

  for (const auto& function : functions) {
    stream << "  " << (function.is_async ? "async " : "") << "fn @" << function.name << '(';
    for (std::size_t index = 0; index < function.parameters.size(); ++index) {
      if (index != 0) stream << ", ";
      stream << function.parameters[index].first << " %" << function.parameters[index].second;
    }
    stream << ") -> " << function.return_type << "\n";
    if (!function.borrow_regions.empty()) {
      stream << "    borrow.regions=" << function.borrow_regions.size() << "\n";
      for (const auto& region : function.borrow_regions) {
        stream << "      borrow.region " << region.id << ' ' << (region.mutable_borrow ? "exclusive" : "shared")
               << " alias=" << region.alias_path << " source=" << region.source_path
               << " begin=" << region.begin_block << ':' << region.begin_instruction
               << " end=" << region.end_block << ':' << region.end_instruction;
        if (region.parent_region >= 0) stream << " parent=" << region.parent_region;
        if (region.contains_back_edge) stream << " back-edge";
        if (region.crosses_suspension) stream << " suspension";
        stream << " uses=" << region.uses.size() << '\n';
      }
    }
    if (function.is_async) {
      stream << "    async.frame slots=" << function.async_frame.size()
             << " states=" << function.suspension_points.size() << "\n";
      for (const auto& slot : function.async_frame) {
        stream << "      slot " << slot.slot << " " << slot.type_name << " %" << slot.value;
        if (slot.owned) {
        stream << " owned";
        if (slot.owned_bytes) stream << ".bytes size=" << slot.storage_size << " align=" << slot.storage_alignment;
        if (slot.projection_count != 0) stream << " projections=" << slot.projection_count;
        stream << " cleanup=@" << slot.cleanup_symbol;
      }
        stream << "\n";
      }
      for (const auto& point : function.suspension_points) {
        stream << "      suspend state=" << point.state << " at " << point.block << ':'
               << point.instruction_index << " live";
        for (const auto& value : point.live_values) stream << " %" << value;
        stream << '\n';
      }
      for (const auto& region : function.async_regions) {
        stream << "      region state=" << region.state << " entry=" << region.entry_block << ':'
               << region.entry_instruction << " blocks";
        for (const auto& block : region.reachable_blocks) stream << ' ' << block;
        if (region.contains_back_edge) stream << " back-edge";
        stream << '\n';
      }
      const auto& machine = function.async_state_machine;
      stream << "    async.machine frame=@" << machine.frame_type
             << " new=@" << machine.constructor << " poll=@" << machine.poll
             << " drop=@" << machine.destroy << "\n";
      for (const auto& state : machine.states) {
        stream << "      state " << state.state << ' ' << mir_async_state_kind_name(state.kind);
        if (!state.block.empty()) stream << " resume=" << state.block << ':' << state.instruction_index;
        stream << '\n';
      }
      for (const auto& generated : machine.generated_functions) {
        stream << "    async.generated " << mir_async_generated_kind_name(generated.kind)
               << " @" << generated.name << " -> " << generated.return_type << '\n';
        for (const auto& generated_block : generated.blocks) {
          stream << "      " << generated_block.name << ":\n";
          for (const auto& instruction : generated_block.instructions) {
            stream << "        ";
            if (!instruction.result.empty()) stream << '%' << instruction.result << " = ";
            stream << mir_opcode_name(instruction.opcode);
            if (!instruction.type_name.empty()) stream << ' ' << instruction.type_name;
            for (const auto& operand : instruction.operands) stream << ' ' << operand;
            stream << '\n';
          }
        }
      }
    }
    for (const auto& block : function.blocks) {
      stream << "    " << block.name << ":\n";
      for (const auto& instruction : block.instructions) {
        stream << "      ";
        if (!instruction.result.empty()) stream << '%' << instruction.result << " = ";
        stream << mir_opcode_name(instruction.opcode);
        if (!instruction.type_name.empty()) stream << ' ' << instruction.type_name;
        for (const auto& operand : instruction.operands) stream << ' ' << operand;
        stream << '\n';
      }
    }
  }
}

}  // namespace raz::compiler
