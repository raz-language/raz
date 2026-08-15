// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "compiler/backend/forge/forge_lowering.hpp"
#include "compiler/diagnostics/diagnostic_engine.hpp"
#include "compiler/ir/mir/mir.hpp"

#include <cstdlib>
#include <iostream>

int main() {
  raz::compiler::MirModule module;
  raz::compiler::MirFunction function;
  function.name = "answer";
  function.return_type = "i64";
  raz::compiler::MirBlock block{"entry", {}};
  block.instructions.push_back({raz::compiler::MirOpcode::constant, "t0", "i64", {"42"}, {}});
  block.instructions.push_back({raz::compiler::MirOpcode::return_value, {}, "i64", {"t0"}, {}});
  function.blocks.push_back(std::move(block));
  module.functions.push_back(std::move(function));
  const auto errors = module.verify();
  if (!errors.empty()) {
    std::cerr << errors.front() << '\n';
    return EXIT_FAILURE;
  }

  raz::compiler::MirModule invalid;
  raz::compiler::MirFunction broken;
  broken.name = "broken";
  broken.blocks.push_back({"entry", {}});
  invalid.functions.push_back(std::move(broken));
  if (invalid.verify().empty()) {
    std::cerr << "verifier accepted an unterminated block\n";
    return EXIT_FAILURE;
  }

  raz::compiler::MirModule malformed_memory;
  raz::compiler::MirFunction memory_function;
  memory_function.name = "malformed_memory";
  raz::compiler::MirBlock memory_block{"entry", {}};
  memory_block.instructions.push_back({raz::compiler::MirOpcode::store, "illegal_result", "i64", {"value"}, {}});
  memory_block.instructions.push_back({raz::compiler::MirOpcode::return_void, {}, {}, {}, {}});
  memory_function.blocks.push_back(std::move(memory_block));
  malformed_memory.functions.push_back(std::move(memory_function));
  if (malformed_memory.verify().empty()) {
    std::cerr << "verifier accepted a malformed store\n";
    return EXIT_FAILURE;
  }

  raz::compiler::MirModule malformed_drop;
  raz::compiler::MirFunction drop_function;
  drop_function.name = "malformed_drop";
  raz::compiler::MirBlock drop_block{"entry", {}};
  drop_block.instructions.push_back({raz::compiler::MirOpcode::drop, "illegal_result", "Resource", {"slot"}, {}});
  drop_block.instructions.push_back({raz::compiler::MirOpcode::return_void, {}, {}, {}, {}});
  drop_function.blocks.push_back(std::move(drop_block));
  malformed_drop.functions.push_back(std::move(drop_function));
  if (malformed_drop.verify().empty()) {
    std::cerr << "verifier accepted a malformed drop\n";
    return EXIT_FAILURE;
  }

  raz::compiler::MirModule use_after_drop;
  raz::compiler::MirFunction use_after_drop_function;
  use_after_drop_function.name = "use_after_drop";
  use_after_drop_function.return_type = "i64";
  raz::compiler::MirBlock use_after_drop_block{"entry", {}};
  use_after_drop_block.instructions.push_back({raz::compiler::MirOpcode::stack_allocate, "slot", "i64", {"8", "8"}, {}});
  use_after_drop_block.instructions.push_back({raz::compiler::MirOpcode::storage_live, {}, "i64", {"slot"}, {}});
  use_after_drop_block.instructions.push_back({raz::compiler::MirOpcode::drop, {}, "i64", {"slot"}, {}});
  use_after_drop_block.instructions.push_back({raz::compiler::MirOpcode::storage_dead, {}, "i64", {"slot"}, {}});
  use_after_drop_block.instructions.push_back({raz::compiler::MirOpcode::load, "late", "i64", {"slot"}, {}});
  use_after_drop_block.instructions.push_back({raz::compiler::MirOpcode::return_value, {}, "i64", {"late"}, {}});
  use_after_drop_function.blocks.push_back(std::move(use_after_drop_block));
  use_after_drop.functions.push_back(std::move(use_after_drop_function));
  if (use_after_drop.verify().empty()) {
    std::cerr << "verifier accepted a use after final drop\n";
    return EXIT_FAILURE;
  }

  raz::compiler::MirModule double_drop;
  raz::compiler::MirFunction double_drop_function;
  double_drop_function.name = "double_drop";
  raz::compiler::MirBlock double_drop_block{"entry", {}};
  double_drop_block.instructions.push_back({raz::compiler::MirOpcode::stack_allocate, "slot", "Resource", {"8", "8"}, {}});
  double_drop_block.instructions.push_back({raz::compiler::MirOpcode::storage_live, {}, "Resource", {"slot"}, {}});
  double_drop_block.instructions.push_back({raz::compiler::MirOpcode::drop, {}, "Resource", {"slot"}, {}});
  double_drop_block.instructions.push_back({raz::compiler::MirOpcode::storage_dead, {}, "Resource", {"slot"}, {}});
  double_drop_block.instructions.push_back({raz::compiler::MirOpcode::drop, {}, "Resource", {"slot"}, {}});
  double_drop_block.instructions.push_back({raz::compiler::MirOpcode::return_void, {}, {}, {}, {}});
  double_drop_function.blocks.push_back(std::move(double_drop_block));
  double_drop.functions.push_back(std::move(double_drop_function));
  if (double_drop.verify().empty()) {
    std::cerr << "verifier accepted a double drop\n";
    return EXIT_FAILURE;
  }

  raz::compiler::MirModule cfg_use_after_move;
  raz::compiler::MirFunction cfg_move_function;
  cfg_move_function.name = "cfg_use_after_move";
  cfg_move_function.return_type = "i64";
  raz::compiler::MirBlock cfg_move_entry{"entry", {}};
  cfg_move_entry.instructions.push_back({raz::compiler::MirOpcode::stack_allocate, "slot", "i64", {"8", "8"}, {}});
  cfg_move_entry.instructions.push_back({raz::compiler::MirOpcode::storage_live, {}, "i64", {"slot"}, {}});
  cfg_move_entry.instructions.push_back({raz::compiler::MirOpcode::constant, "condition", "bool", {"true"}, {}});
  cfg_move_entry.instructions.push_back({raz::compiler::MirOpcode::branch, {}, "bool", {"condition", "moved", "live"}, {}});
  raz::compiler::MirBlock cfg_moved{"moved", {}};
  cfg_moved.instructions.push_back({raz::compiler::MirOpcode::move_value, {}, "i64", {"slot", "value"}, {}});
  cfg_moved.instructions.push_back({raz::compiler::MirOpcode::jump, {}, {}, {"merge"}, {}});
  raz::compiler::MirBlock cfg_live{"live", {}};
  cfg_live.instructions.push_back({raz::compiler::MirOpcode::jump, {}, {}, {"merge"}, {}});
  raz::compiler::MirBlock cfg_merge{"merge", {}};
  cfg_merge.instructions.push_back({raz::compiler::MirOpcode::load, "late", "i64", {"slot"}, {}});
  cfg_merge.instructions.push_back({raz::compiler::MirOpcode::return_value, {}, "i64", {"late"}, {}});
  cfg_move_function.blocks.push_back(std::move(cfg_move_entry));
  cfg_move_function.blocks.push_back(std::move(cfg_moved));
  cfg_move_function.blocks.push_back(std::move(cfg_live));
  cfg_move_function.blocks.push_back(std::move(cfg_merge));
  cfg_use_after_move.functions.push_back(std::move(cfg_move_function));
  if (cfg_use_after_move.verify().empty()) {
    std::cerr << "verifier accepted a CFG-joined use after move\n";
    return EXIT_FAILURE;
  }

  raz::compiler::MirModule reinitialized_move;
  raz::compiler::MirFunction reinitialized_function;
  reinitialized_function.name = "reinitialized_move";
  reinitialized_function.return_type = "i64";
  raz::compiler::MirBlock reinitialized_block{"entry", {}};
  reinitialized_block.instructions.push_back({raz::compiler::MirOpcode::stack_allocate, "slot", "i64", {"8", "8"}, {}});
  reinitialized_block.instructions.push_back({raz::compiler::MirOpcode::storage_live, {}, "i64", {"slot"}, {}});
  reinitialized_block.instructions.push_back({raz::compiler::MirOpcode::move_value, {}, "i64", {"slot", "value"}, {}});
  reinitialized_block.instructions.push_back({raz::compiler::MirOpcode::constant, "replacement", "i64", {"9"}, {}});
  reinitialized_block.instructions.push_back({raz::compiler::MirOpcode::store, {}, "i64", {"replacement", "slot"}, {}});
  reinitialized_block.instructions.push_back({raz::compiler::MirOpcode::load, "loaded", "i64", {"slot"}, {}});
  reinitialized_block.instructions.push_back({raz::compiler::MirOpcode::return_value, {}, "i64", {"loaded"}, {}});
  reinitialized_function.blocks.push_back(std::move(reinitialized_block));
  reinitialized_move.functions.push_back(std::move(reinitialized_function));
  if (!reinitialized_move.verify().empty()) {
    std::cerr << "verifier rejected storage reinitialization after move\n";
    return EXIT_FAILURE;
  }

  // MIR lifetime-region inference: the source must outlive every alias use.
  raz::compiler::MirModule region_outlives_source;
  raz::compiler::MirFunction region_function;
  region_function.name = "region_outlives_source";
  region_function.return_type = "i64";
  raz::compiler::MirBlock region_block{"entry", {}};
  region_block.instructions.push_back({raz::compiler::MirOpcode::stack_allocate, "source", "i64", {"8", "8"}, {}});
  region_block.instructions.push_back({raz::compiler::MirOpcode::storage_live, {}, "i64", {"source"}, {}});
  region_block.instructions.push_back({raz::compiler::MirOpcode::place_path, {}, "i64", {"source", "source"}, {}});
  region_block.instructions.push_back({raz::compiler::MirOpcode::stack_allocate, "alias", "i64&", {"8", "8"}, {}});
  region_block.instructions.push_back({raz::compiler::MirOpcode::storage_live, {}, "i64&", {"alias"}, {}});
  region_block.instructions.push_back({raz::compiler::MirOpcode::place_path, {}, "i64&", {"alias", "alias"}, {}});
  region_block.instructions.push_back({raz::compiler::MirOpcode::store, {}, "i64&", {"source", "alias"}, {}});
  region_block.instructions.push_back({raz::compiler::MirOpcode::borrow_bind, {}, "i64&", {"alias", "source", "source", "alias"}, {}});
  region_block.instructions.push_back({raz::compiler::MirOpcode::storage_dead, {}, "i64", {"source"}, {}});
  region_block.instructions.push_back({raz::compiler::MirOpcode::load, "dangling", "i64&", {"alias"}, {}});
  region_block.instructions.push_back({raz::compiler::MirOpcode::return_void, {}, {}, {}, {}});
  region_function.blocks.push_back(std::move(region_block));
  raz::compiler::mir_infer_borrow_regions(region_function);
  if (region_function.borrow_regions.size() != 1 || region_function.borrow_regions[0].uses.empty()) {
    std::cerr << "borrow region inference did not discover alias uses\n";
    return EXIT_FAILURE;
  }
  region_outlives_source.functions.push_back(std::move(region_function));
  if (region_outlives_source.verify().empty()) {
    std::cerr << "verifier accepted a borrow region that outlives its source\n";
    return EXIT_FAILURE;
  }

  // Reinitialization/source lifetime that covers the full alias region is legal.
  raz::compiler::MirModule valid_region;
  raz::compiler::MirFunction valid_region_function;
  valid_region_function.name = "valid_region";
  valid_region_function.return_type = "i64";
  raz::compiler::MirBlock valid_region_block{"entry", {}};
  valid_region_block.instructions.push_back({raz::compiler::MirOpcode::stack_allocate, "source", "i64", {"8", "8"}, {}});
  valid_region_block.instructions.push_back({raz::compiler::MirOpcode::storage_live, {}, "i64", {"source"}, {}});
  valid_region_block.instructions.push_back({raz::compiler::MirOpcode::place_path, {}, "i64", {"source", "source"}, {}});
  valid_region_block.instructions.push_back({raz::compiler::MirOpcode::stack_allocate, "alias", "i64&", {"8", "8"}, {}});
  valid_region_block.instructions.push_back({raz::compiler::MirOpcode::storage_live, {}, "i64&", {"alias"}, {}});
  valid_region_block.instructions.push_back({raz::compiler::MirOpcode::place_path, {}, "i64&", {"alias", "alias"}, {}});
  valid_region_block.instructions.push_back({raz::compiler::MirOpcode::store, {}, "i64&", {"source", "alias"}, {}});
  valid_region_block.instructions.push_back({raz::compiler::MirOpcode::borrow_bind, {}, "i64&", {"alias", "source", "source", "alias"}, {}});
  valid_region_block.instructions.push_back({raz::compiler::MirOpcode::load, "borrowed", "i64&", {"alias"}, {}});
  valid_region_block.instructions.push_back({raz::compiler::MirOpcode::storage_dead, {}, "i64&", {"alias"}, {}});
  valid_region_block.instructions.push_back({raz::compiler::MirOpcode::storage_dead, {}, "i64", {"source"}, {}});
  valid_region_block.instructions.push_back({raz::compiler::MirOpcode::return_void, {}, {}, {}, {}});
  valid_region_function.blocks.push_back(std::move(valid_region_block));
  raz::compiler::mir_infer_borrow_regions(valid_region_function);
  valid_region.functions.push_back(std::move(valid_region_function));
  if (!valid_region.verify().empty()) {
    std::cerr << "verifier rejected a borrow whose source outlives the inferred region\n";
    return EXIT_FAILURE;
  }

  raz::compiler::MirModule malformed_async;
  raz::compiler::MirFunction sync_function;
  sync_function.name = "sync_with_await";
  raz::compiler::MirBlock async_block{"entry", {}};
  async_block.instructions.push_back({raz::compiler::MirOpcode::async_await, "result", "i64", {"value"}, {}});
  async_block.instructions.push_back({raz::compiler::MirOpcode::return_value, {}, "i64", {"result"}, {}});
  sync_function.blocks.push_back(std::move(async_block));
  malformed_async.functions.push_back(std::move(sync_function));
  if (malformed_async.verify().empty()) {
    std::cerr << "verifier accepted async await in a non-async function\n";
    return EXIT_FAILURE;
  }

  raz::compiler::MirFunction resumable;
  resumable.name = "resumable";
  resumable.return_type = "i64";
  resumable.is_async = true;
  resumable.parameters.push_back({"i64", "input"});
  raz::compiler::MirBlock resumable_block{"entry", {}};
  resumable_block.instructions.push_back({raz::compiler::MirOpcode::copy, "before", "i64", {"input"}, {}});
  resumable_block.instructions.push_back({raz::compiler::MirOpcode::async_await, "ready", "i64", {"before"}, {}});
  resumable_block.instructions.push_back({raz::compiler::MirOpcode::add, "sum", "i64", {"before", "ready"}, {}});
  resumable_block.instructions.push_back({raz::compiler::MirOpcode::return_value, {}, "i64", {"sum"}, {}});
  resumable.blocks.push_back(std::move(resumable_block));
  raz::compiler::mir_analyze_async_frame(resumable);
  raz::compiler::mir_synthesize_async_state_machine(resumable);
  if (resumable.suspension_points.size() != 1 || resumable.suspension_points[0].state != 1 ||
      resumable.async_frame.size() != 2 || resumable.async_frame[0].value != "input" ||
      resumable.async_frame[1].value != "before" ||
      resumable.async_state_machine.states.size() != 4 ||
      resumable.async_state_machine.states[1].kind != raz::compiler::MirAsyncStateKind::suspended ||
      resumable.async_state_machine.states[2].kind != raz::compiler::MirAsyncStateKind::completed ||
      resumable.async_state_machine.states[3].kind != raz::compiler::MirAsyncStateKind::cancelled ||
      resumable.async_state_machine.generated_functions.size() != 3 ||
      resumable.async_state_machine.generated_functions[0].blocks.empty() ||
      resumable.async_state_machine.generated_functions[1].blocks.size() < 12 ||
      resumable.async_state_machine.generated_functions[2].blocks.empty()) {
    std::cerr << "async frame liveness analysis failed\n";
    return EXIT_FAILURE;
  }

  bool saw_slot_store = false;
  bool saw_slot_load = false;
  bool saw_cancel_check = false;
  bool saw_result_load = false;
  bool saw_frame_cancel = false;
  bool saw_await_poll = false;
  bool saw_await_result = false;
  for (const auto& generated_block : resumable.async_state_machine.generated_functions[1].blocks) {
    for (const auto& instruction : generated_block.instructions) {
      saw_slot_store = saw_slot_store || instruction.opcode == raz::compiler::MirOpcode::async_slot_store;
      saw_slot_load = saw_slot_load || instruction.opcode == raz::compiler::MirOpcode::async_slot_load;
      saw_cancel_check = saw_cancel_check || instruction.opcode == raz::compiler::MirOpcode::async_cancel_requested;
      saw_result_load = saw_result_load || instruction.opcode == raz::compiler::MirOpcode::async_result_load;
      saw_frame_cancel = saw_frame_cancel || instruction.opcode == raz::compiler::MirOpcode::async_frame_cancel;
      saw_await_poll = saw_await_poll || instruction.opcode == raz::compiler::MirOpcode::async_await_poll;
      saw_await_result = saw_await_result || instruction.opcode == raz::compiler::MirOpcode::async_await_result;
    }
  }

  if (!saw_slot_store || !saw_slot_load || !saw_cancel_check || !saw_result_load || !saw_frame_cancel ||
      !saw_await_poll || !saw_await_result) {
    std::cerr << "async frame lifecycle synthesis failed\n";
    return EXIT_FAILURE;
  }
  raz::compiler::MirFunction cfg_async;
  cfg_async.name = "cfg_async";
  cfg_async.return_type = "i64";
  cfg_async.is_async = true;
  cfg_async.parameters.push_back({"i64", "future"});
  raz::compiler::MirBlock cfg_entry{"entry", {}};
  cfg_entry.instructions.push_back({raz::compiler::MirOpcode::constant, "condition", "bool", {"1"}, {}});
  cfg_entry.instructions.push_back({raz::compiler::MirOpcode::branch, {}, {}, {"condition", "loop", "exit"}, {}});
  raz::compiler::MirBlock cfg_loop{"loop", {}};
  cfg_loop.instructions.push_back({raz::compiler::MirOpcode::async_await, "value", "i64", {"future"}, {}});
  cfg_loop.instructions.push_back({raz::compiler::MirOpcode::branch, {}, {}, {"condition", "loop", "exit"}, {}});
  raz::compiler::MirBlock cfg_exit{"exit", {}};
  cfg_exit.instructions.push_back({raz::compiler::MirOpcode::return_value, {}, "i64", {"future"}, {}});
  cfg_async.blocks.push_back(std::move(cfg_entry));
  cfg_async.blocks.push_back(std::move(cfg_loop));
  cfg_async.blocks.push_back(std::move(cfg_exit));
  raz::compiler::mir_analyze_async_frame(cfg_async);
  raz::compiler::mir_synthesize_async_state_machine(cfg_async);
  if (cfg_async.async_regions.size() != 2 || cfg_async.async_regions[0].reachable_blocks.size() != 3 ||
      !cfg_async.async_regions[0].contains_back_edge || !cfg_async.async_regions[1].contains_back_edge ||
      cfg_async.async_regions[1].entry_block != "loop" || cfg_async.async_regions[1].entry_instruction != 1) {
    std::cerr << "async CFG region analysis failed\n";
    return EXIT_FAILURE;
  }
  bool saw_cfg_loop = false;
  bool saw_cfg_exit = false;
  bool saw_cfg_fallback = false;
  for (const auto& generated_block : cfg_async.async_state_machine.generated_functions[1].blocks) {
    saw_cfg_loop = saw_cfg_loop || generated_block.name == "state.0.cfg.loop" || generated_block.name == "state.1.cfg.loop";
    saw_cfg_exit = saw_cfg_exit || generated_block.name == "state.0.cfg.exit" || generated_block.name == "state.1.cfg.exit";
    for (const auto& instruction : generated_block.instructions) {
      if (instruction.result.starts_with("status.fallback.")) saw_cfg_fallback = true;
    }
  }

  if (!saw_cfg_loop || !saw_cfg_exit || saw_cfg_fallback) {
    std::cerr << "async CFG executable region synthesis failed\n";
    return EXIT_FAILURE;
  }

  raz::compiler::MirFunction allocated_cfg;
  allocated_cfg.name = "allocated_cfg";
  allocated_cfg.return_type = "i64";
  allocated_cfg.is_async = true;
  allocated_cfg.parameters.push_back({"i64", "future"});
  raz::compiler::MirBlock allocated_entry{"entry", {}};
  allocated_entry.instructions.push_back({raz::compiler::MirOpcode::stack_allocate, "storage", "i64", {"8", "8"}, {}});
  allocated_entry.instructions.push_back({raz::compiler::MirOpcode::store, {}, "i64", {"storage", "future"}, {}});
  allocated_entry.instructions.push_back({raz::compiler::MirOpcode::jump, {}, {}, {"wait"}, {}});
  raz::compiler::MirBlock allocated_wait{"wait", {}};
  allocated_wait.instructions.push_back({raz::compiler::MirOpcode::async_await, "ready", "i64", {"future"}, {}});
  allocated_wait.instructions.push_back({raz::compiler::MirOpcode::load, "saved", "i64", {"storage"}, {}});
  allocated_wait.instructions.push_back({raz::compiler::MirOpcode::return_value, {}, "i64", {"saved"}, {}});
  allocated_cfg.blocks.push_back(std::move(allocated_entry));
  allocated_cfg.blocks.push_back(std::move(allocated_wait));
  raz::compiler::mir_analyze_async_frame(allocated_cfg);
  raz::compiler::mir_synthesize_async_state_machine(allocated_cfg);
  bool saw_promoted_address = false;
  bool saw_allocated_fallback = false;
  for (const auto& generated_block : allocated_cfg.async_state_machine.generated_functions[1].blocks) {
    for (const auto& instruction : generated_block.instructions) {
      saw_promoted_address = saw_promoted_address || instruction.opcode == raz::compiler::MirOpcode::async_slot_address;
      if (instruction.result.starts_with("status.fallback.")) saw_allocated_fallback = true;
    }
  }

  if (!saw_promoted_address || saw_allocated_fallback) {
    std::cerr << "cross-suspension allocation promotion failed\n";
    return EXIT_FAILURE;
  }

  auto malformed_projection = resumable;
  bool corrupted_projection = false;
  for (auto& generated : malformed_projection.async_state_machine.generated_functions) {
    for (auto& generated_block : generated.blocks) {
      for (auto& instruction : generated_block.instructions) {
        if (instruction.opcode == raz::compiler::MirOpcode::async_slot_projection_batch_set) {
          instruction.operands[3] = "999999";
          corrupted_projection = true;
          break;
        }
      }
      if (corrupted_projection) break;
    }

    if (corrupted_projection) break;
  }

  if (corrupted_projection) {
    raz::compiler::MirModule malformed_projection_module;
    malformed_projection_module.functions.push_back(std::move(malformed_projection));
    if (malformed_projection_module.verify().empty()) {
      std::cerr << "verifier accepted an out-of-range generated async projection batch\n";
      return EXIT_FAILURE;
    }
  }

  auto mismatched_projected_allocation = resumable;
  bool corrupted_allocation = false;
  for (auto& generated : mismatched_projected_allocation.async_state_machine.generated_functions) {
    for (auto& generated_block : generated.blocks) {
      for (auto& instruction : generated_block.instructions) {
        if (instruction.opcode == raz::compiler::MirOpcode::async_slot_allocate_projected_bytes) {
          instruction.operands[2] = "999999";
          corrupted_allocation = true;
          break;
        }
      }
      if (corrupted_allocation) break;
    }

    if (corrupted_allocation) break;
  }

  if (corrupted_allocation) {
    raz::compiler::MirModule mismatched_projected_allocation_module;
    mismatched_projected_allocation_module.functions.push_back(std::move(mismatched_projected_allocation));
    if (mismatched_projected_allocation_module.verify().empty()) {
      std::cerr << "verifier accepted mismatched projected async allocation metadata\n";
      return EXIT_FAILURE;
    }
  }

  auto invalid_projection_state_type = resumable;
  bool corrupted_state_type = false;
  for (auto& generated : invalid_projection_state_type.async_state_machine.generated_functions) {
    for (auto& generated_block : generated.blocks) {
      for (auto& instruction : generated_block.instructions) {
        if (instruction.opcode == raz::compiler::MirOpcode::async_slot_projection_batch_set) {
          const auto state_name = instruction.operands[5];
          for (auto& defining_block : generated.blocks) {
            for (auto& defining : defining_block.instructions) {
              if (defining.result == state_name) {
                defining.type_name = "i64";
                corrupted_state_type = true;
                break;
              }
            }
            if (corrupted_state_type) break;
          }
          break;
        }
      }
      if (corrupted_state_type) break;
    }

    if (corrupted_state_type) break;
  }

  if (corrupted_state_type) {
    raz::compiler::MirModule invalid_projection_state_type_module;
    invalid_projection_state_type_module.functions.push_back(std::move(invalid_projection_state_type));
    if (invalid_projection_state_type_module.verify().empty()) {
      std::cerr << "verifier accepted a non-bool async projection state value\n";
      return EXIT_FAILURE;
    }
  }

  auto nondominating_projected_allocation = resumable;
  bool relocated_projected_allocation = false;
  for (auto& generated : nondominating_projected_allocation.async_state_machine.generated_functions) {
    if (generated.kind != raz::compiler::MirAsyncGeneratedKind::poll) continue;
    raz::compiler::MirInstruction allocation;
    for (auto& generated_block : generated.blocks) {
      for (auto instruction = generated_block.instructions.begin(); instruction != generated_block.instructions.end(); ++instruction) {
        if (instruction->opcode == raz::compiler::MirOpcode::async_slot_allocate_projected_bytes) {
          allocation = *instruction;
          generated_block.instructions.erase(instruction);
          relocated_projected_allocation = true;
          break;
        }
      }
      if (relocated_projected_allocation) break;
    }

    if (relocated_projected_allocation) {
      raz::compiler::MirBlock detached{"detached.projected.allocation", {}};
      detached.instructions.push_back(std::move(allocation));
      detached.instructions.push_back({raz::compiler::MirOpcode::unreachable, {}, {}, {}, {}});
      generated.blocks.push_back(std::move(detached));
    }
    break;
  }

  if (relocated_projected_allocation) {
    raz::compiler::MirModule nondominating_projected_allocation_module;
    nondominating_projected_allocation_module.functions.push_back(std::move(nondominating_projected_allocation));
    if (nondominating_projected_allocation_module.verify().empty()) {
      std::cerr << "verifier accepted a projected async allocation that does not dominate its updates\n";
      return EXIT_FAILURE;
    }
  }

  auto nondominating_projection_state = resumable;
  bool relocated_projection_state = false;
  for (auto& generated : nondominating_projection_state.async_state_machine.generated_functions) {
    if (generated.kind != raz::compiler::MirAsyncGeneratedKind::poll) continue;
    std::string state_name;
    for (const auto& generated_block : generated.blocks) {
      for (const auto& instruction : generated_block.instructions) {
        if (instruction.opcode == raz::compiler::MirOpcode::async_slot_projection_batch_set &&
            instruction.operands.size() >= 6) {
          state_name = instruction.operands[5];
          break;
        }
      }
      if (!state_name.empty()) break;
    }
    raz::compiler::MirInstruction state_definition;
    if (!state_name.empty()) {
      for (auto& generated_block : generated.blocks) {
        for (auto instruction = generated_block.instructions.begin(); instruction != generated_block.instructions.end(); ++instruction) {
          if (instruction->result == state_name) {
            state_definition = *instruction;
            generated_block.instructions.erase(instruction);
            relocated_projection_state = true;
            break;
          }
        }
        if (relocated_projection_state) break;
      }
    }

    if (relocated_projection_state) {
      raz::compiler::MirBlock detached{"detached.projection.state", {}};
      detached.instructions.push_back(std::move(state_definition));
      detached.instructions.push_back({raz::compiler::MirOpcode::unreachable, {}, {}, {}, {}});
      generated.blocks.push_back(std::move(detached));
    }
    break;
  }

  if (relocated_projection_state) {
    raz::compiler::MirModule nondominating_projection_state_module;
    nondominating_projection_state_module.functions.push_back(std::move(nondominating_projection_state));
    if (nondominating_projection_state_module.verify().empty()) {
      std::cerr << "verifier accepted a projection state value that does not dominate its update\n";
      return EXIT_FAILURE;
    }
  }

  auto cyclic_projected_allocation = resumable;
  bool made_projected_allocation_cyclic = false;
  for (auto& generated : cyclic_projected_allocation.async_state_machine.generated_functions) {
    if (generated.kind != raz::compiler::MirAsyncGeneratedKind::poll) continue;
    for (auto& generated_block : generated.blocks) {
      bool has_projected_allocation = false;
      for (const auto& instruction : generated_block.instructions) {
        if (instruction.opcode == raz::compiler::MirOpcode::async_slot_allocate_projected_bytes) {
          has_projected_allocation = true;
          break;
        }
      }
      if (!has_projected_allocation || generated_block.instructions.empty()) continue;
      generated_block.instructions.back() = {raz::compiler::MirOpcode::jump, {}, {}, {generated_block.name}, {}};
      made_projected_allocation_cyclic = true;
      break;
    }
    break;
  }

  if (made_projected_allocation_cyclic) {
    raz::compiler::MirModule cyclic_projected_allocation_module;
    cyclic_projected_allocation_module.functions.push_back(std::move(cyclic_projected_allocation));
    if (cyclic_projected_allocation_module.verify().empty()) {
      std::cerr << "verifier accepted a projected async allocation inside a control-flow cycle\n";
      return EXIT_FAILURE;
    }
  }

  auto invalid_projected_load = resumable;
  bool replaced_transfer_with_load = false;
  for (auto& generated : invalid_projected_load.async_state_machine.generated_functions) {
    if (generated.kind != raz::compiler::MirAsyncGeneratedKind::poll) continue;
    for (auto& generated_block : generated.blocks) {
      for (auto& instruction : generated_block.instructions) {
        if (instruction.opcode != raz::compiler::MirOpcode::async_slot_transfer_projected ||
            instruction.operands.size() != 2) continue;
        instruction.opcode = raz::compiler::MirOpcode::async_slot_load;
        replaced_transfer_with_load = true;
        break;
      }
      if (replaced_transfer_with_load) break;
    }

    if (replaced_transfer_with_load) break;
  }

  if (replaced_transfer_with_load) {
    raz::compiler::MirModule invalid_projected_load_module;
    invalid_projected_load_module.functions.push_back(std::move(invalid_projected_load));
    if (invalid_projected_load_module.verify().empty()) {
      std::cerr << "verifier accepted a non-atomic projected async slot load\n";
      return EXIT_FAILURE;
    }
  }

  auto invalid_projected_take = resumable;
  bool replaced_transfer_with_take = false;
  for (auto& generated : invalid_projected_take.async_state_machine.generated_functions) {
    if (generated.kind != raz::compiler::MirAsyncGeneratedKind::poll) continue;
    for (auto& generated_block : generated.blocks) {
      for (auto& instruction : generated_block.instructions) {
        if (instruction.opcode != raz::compiler::MirOpcode::async_slot_transfer_projected ||
            instruction.operands.size() != 2) continue;
        instruction.opcode = raz::compiler::MirOpcode::async_slot_take;
        replaced_transfer_with_take = true;
        break;
      }
      if (replaced_transfer_with_take) break;
    }

    if (replaced_transfer_with_take) break;
  }

  if (replaced_transfer_with_take) {
    raz::compiler::MirModule invalid_projected_take_module;
    invalid_projected_take_module.functions.push_back(std::move(invalid_projected_take));
    if (invalid_projected_take_module.verify().empty()) {
      std::cerr << "verifier accepted destructive take on projected async byte storage\n";
      return EXIT_FAILURE;
    }
  }

  auto projected_use_after_transfer = resumable;
  bool inserted_use_after_transfer = false;
  for (auto& generated : projected_use_after_transfer.async_state_machine.generated_functions) {
    if (generated.kind != raz::compiler::MirAsyncGeneratedKind::poll) continue;
    for (auto& generated_block : generated.blocks) {
      for (auto instruction = generated_block.instructions.begin(); instruction != generated_block.instructions.end(); ++instruction) {
        if (instruction->opcode != raz::compiler::MirOpcode::async_slot_transfer_projected ||
            instruction->operands.size() != 2) continue;
        raz::compiler::MirInstruction stale_update{
          raz::compiler::MirOpcode::async_slot_projection_set,
          {},
          {},
          {"frame", instruction->operands[1], "0", "true"},
          {}};
        generated_block.instructions.insert(std::next(instruction), std::move(stale_update));
        inserted_use_after_transfer = true;
        break;
      }
      if (inserted_use_after_transfer) break;
    }

    if (inserted_use_after_transfer) break;
  }

  if (inserted_use_after_transfer) {
    raz::compiler::MirModule projected_use_after_transfer_module;
    projected_use_after_transfer_module.functions.push_back(std::move(projected_use_after_transfer));
    if (projected_use_after_transfer_module.verify().empty()) {
      std::cerr << "verifier accepted projected slot state publication after ownership transfer\n";
      return EXIT_FAILURE;
    }
  }

  auto duplicate_projected_transfer = resumable;
  bool inserted_duplicate_transfer = false;
  for (auto& generated : duplicate_projected_transfer.async_state_machine.generated_functions) {
    if (generated.kind != raz::compiler::MirAsyncGeneratedKind::poll) continue;
    for (auto& generated_block : generated.blocks) {
      for (auto instruction = generated_block.instructions.begin(); instruction != generated_block.instructions.end(); ++instruction) {
        if (instruction->opcode != raz::compiler::MirOpcode::async_slot_transfer_projected ||
            instruction->operands.size() != 2) continue;
        auto duplicate = *instruction;
        duplicate.result += ".duplicate";
        generated_block.instructions.insert(std::next(instruction), std::move(duplicate));
        inserted_duplicate_transfer = true;
        break;
      }
      if (inserted_duplicate_transfer) break;
    }

    if (inserted_duplicate_transfer) break;
  }

  if (inserted_duplicate_transfer) {
    raz::compiler::MirModule duplicate_projected_transfer_module;
    duplicate_projected_transfer_module.functions.push_back(std::move(duplicate_projected_transfer));
    if (duplicate_projected_transfer_module.verify().empty()) {
      std::cerr << "verifier accepted a second projected-slot ownership transfer\n";
      return EXIT_FAILURE;
    }
  }

  auto invalid_projected_slot = resumable;
  if (!invalid_projected_slot.async_frame.empty()) {
    auto& slot = invalid_projected_slot.async_frame.front();
    slot.projection_count = 1;
    slot.owned = false;
    slot.owned_bytes = false;
    slot.cleanup_symbol.clear();
    raz::compiler::MirModule invalid_projected_slot_module;
    invalid_projected_slot_module.functions.push_back(std::move(invalid_projected_slot));
    if (invalid_projected_slot_module.verify().empty()) {
      std::cerr << "verifier accepted projected metadata on an unowned async slot\n";
      return EXIT_FAILURE;
    }
  }

  raz::compiler::MirModule indirect_module;
  raz::compiler::MirFunction target_function;
  target_function.name = "target";
  target_function.return_type = "i64";
  target_function.parameters.push_back({"i64", "value"});
  raz::compiler::MirBlock target_block{"entry", {}};
  target_block.instructions.push_back({raz::compiler::MirOpcode::return_value, {}, "i64", {"value"}, {}});
  target_function.blocks.push_back(std::move(target_block));
  raz::compiler::MirFunction caller_function;
  caller_function.name = "caller";
  caller_function.return_type = "i64";
  raz::compiler::MirBlock caller_block{"entry", {}};
  caller_block.instructions.push_back({raz::compiler::MirOpcode::function_address, "pointer", "fn(i64)->i64", {"target"}, {}});
  caller_block.instructions.push_back({raz::compiler::MirOpcode::constant, "argument", "i64", {"42"}, {}});
  caller_block.instructions.push_back({raz::compiler::MirOpcode::call_indirect, "result", "i64", {"pointer", "target", "argument"}, {}});
  caller_block.instructions.push_back({raz::compiler::MirOpcode::return_value, {}, "i64", {"result"}, {}});
  caller_function.blocks.push_back(std::move(caller_block));
  indirect_module.functions.push_back(std::move(target_function));
  indirect_module.functions.push_back(std::move(caller_function));
  if (!indirect_module.verify().empty()) {
    std::cerr << "verifier rejected typed indirect call MIR\n";
    return EXIT_FAILURE;
  }

  raz::compiler::MirModule signedness_module;
  raz::compiler::MirFunction signed_less;
  signed_less.name = "signed_less";
  signed_less.return_type = "bool";
  signed_less.parameters.push_back({"i64", "lhs"});
  signed_less.parameters.push_back({"i64", "rhs"});
  raz::compiler::MirBlock signed_less_block{"entry", {}};
  signed_less_block.instructions.push_back({raz::compiler::MirOpcode::less, "result", "i64", {"lhs", "rhs"}, {}});
  signed_less_block.instructions.push_back({raz::compiler::MirOpcode::return_value, {}, "bool", {"result"}, {}});
  signed_less.blocks.push_back(std::move(signed_less_block));
  signedness_module.functions.push_back(std::move(signed_less));

  raz::compiler::MirFunction unsigned_less;
  unsigned_less.name = "unsigned_less";
  unsigned_less.return_type = "bool";
  unsigned_less.parameters.push_back({"u64", "lhs"});
  unsigned_less.parameters.push_back({"u64", "rhs"});
  raz::compiler::MirBlock unsigned_less_block{"entry", {}};
  unsigned_less_block.instructions.push_back({raz::compiler::MirOpcode::less, "result", "u64", {"lhs", "rhs"}, {}});
  unsigned_less_block.instructions.push_back({raz::compiler::MirOpcode::return_value, {}, "bool", {"result"}, {}});
  unsigned_less.blocks.push_back(std::move(unsigned_less_block));
  signedness_module.functions.push_back(std::move(unsigned_less));

  raz::compiler::DiagnosticEngine signedness_diagnostics;
  raz::compiler::ForgeLowering signedness_lowering(signedness_diagnostics);
  const auto signedness_ir = signedness_lowering.lower_and_print(signedness_module);
  if (signedness_diagnostics.has_errors() || signedness_ir.find("cmp.lt i64") == std::string::npos ||
      signedness_ir.find("cmp.ult i64") == std::string::npos) {
    std::cerr << "Forge lowering lost signed/unsigned integer comparison semantics\n";
    return EXIT_FAILURE;
  }

  // Projection-granular ownership: moving one field must not poison a disjoint sibling.
  raz::compiler::MirModule disjoint_projection;
  raz::compiler::MirFunction disjoint_function;
  disjoint_function.name = "disjoint_projection";
  disjoint_function.return_type = "i64";
  raz::compiler::MirBlock disjoint_entry{"entry", {}};
  disjoint_entry.instructions.push_back({raz::compiler::MirOpcode::stack_allocate, "pair", "Pair", {"16", "8"}, {}});
  disjoint_entry.instructions.push_back({raz::compiler::MirOpcode::storage_live, {}, "Pair", {"pair"}, {}});
  disjoint_entry.instructions.push_back({raz::compiler::MirOpcode::place_path, {}, "Pair", {"pair", "value"}, {}});
  disjoint_entry.instructions.push_back({raz::compiler::MirOpcode::pointer_offset, "left", "i64", {"pair", "0"}, {}});
  disjoint_entry.instructions.push_back({raz::compiler::MirOpcode::place_path, {}, "i64", {"left", "value.left"}, {}});
  disjoint_entry.instructions.push_back({raz::compiler::MirOpcode::pointer_offset, "right", "i64", {"pair", "8"}, {}});
  disjoint_entry.instructions.push_back({raz::compiler::MirOpcode::place_path, {}, "i64", {"right", "value.right"}, {}});
  disjoint_entry.instructions.push_back({raz::compiler::MirOpcode::move_value, {}, "i64", {"left", "value.left"}, {}});
  disjoint_entry.instructions.push_back({raz::compiler::MirOpcode::jump, {}, {}, {"after"}, {}});
  raz::compiler::MirBlock disjoint_after{"after", {}};
  disjoint_after.instructions.push_back({raz::compiler::MirOpcode::load, "kept", "i64", {"right"}, {}});
  disjoint_after.instructions.push_back({raz::compiler::MirOpcode::return_value, {}, "i64", {"kept"}, {}});
  disjoint_function.blocks.push_back(std::move(disjoint_entry));
  disjoint_function.blocks.push_back(std::move(disjoint_after));
  disjoint_projection.functions.push_back(std::move(disjoint_function));
  if (!disjoint_projection.verify().empty()) {
    std::cerr << "verifier rejected disjoint field use after partial move\n";
    return EXIT_FAILURE;
  }

  // A whole-value use overlaps every moved child projection and must be rejected.
  raz::compiler::MirModule whole_after_partial_move;
  raz::compiler::MirFunction whole_function;
  whole_function.name = "whole_after_partial_move";
  whole_function.return_type = "Pair";
  raz::compiler::MirBlock whole_entry{"entry", {}};
  whole_entry.instructions.push_back({raz::compiler::MirOpcode::stack_allocate, "pair", "Pair", {"16", "8"}, {}});
  whole_entry.instructions.push_back({raz::compiler::MirOpcode::storage_live, {}, "Pair", {"pair"}, {}});
  whole_entry.instructions.push_back({raz::compiler::MirOpcode::place_path, {}, "Pair", {"pair", "value"}, {}});
  whole_entry.instructions.push_back({raz::compiler::MirOpcode::pointer_offset, "left", "i64", {"pair", "0"}, {}});
  whole_entry.instructions.push_back({raz::compiler::MirOpcode::place_path, {}, "i64", {"left", "value.left"}, {}});
  whole_entry.instructions.push_back({raz::compiler::MirOpcode::move_value, {}, "i64", {"left", "value.left"}, {}});
  whole_entry.instructions.push_back({raz::compiler::MirOpcode::jump, {}, {}, {"after"}, {}});
  raz::compiler::MirBlock whole_after{"after", {}};
  whole_after.instructions.push_back({raz::compiler::MirOpcode::return_value, {}, "Pair", {"pair"}, {}});
  whole_function.blocks.push_back(std::move(whole_entry));
  whole_function.blocks.push_back(std::move(whole_after));
  whole_after_partial_move.functions.push_back(std::move(whole_function));
  if (whole_after_partial_move.verify().empty()) {
    std::cerr << "verifier accepted whole-value use after partial move\n";
    return EXIT_FAILURE;
  }

  // Reinitializing a moved field restores exactly that projection.
  raz::compiler::MirModule projection_reinit;
  raz::compiler::MirFunction projection_reinit_function;
  projection_reinit_function.name = "projection_reinit";
  projection_reinit_function.return_type = "i64";
  raz::compiler::MirBlock projection_reinit_entry{"entry", {}};
  projection_reinit_entry.instructions.push_back({raz::compiler::MirOpcode::stack_allocate, "pair", "Pair", {"16", "8"}, {}});
  projection_reinit_entry.instructions.push_back({raz::compiler::MirOpcode::storage_live, {}, "Pair", {"pair"}, {}});
  projection_reinit_entry.instructions.push_back({raz::compiler::MirOpcode::place_path, {}, "Pair", {"pair", "value"}, {}});
  projection_reinit_entry.instructions.push_back({raz::compiler::MirOpcode::pointer_offset, "left", "i64", {"pair", "0"}, {}});
  projection_reinit_entry.instructions.push_back({raz::compiler::MirOpcode::place_path, {}, "i64", {"left", "value.left"}, {}});
  projection_reinit_entry.instructions.push_back({raz::compiler::MirOpcode::move_value, {}, "i64", {"left", "value.left"}, {}});
  projection_reinit_entry.instructions.push_back({raz::compiler::MirOpcode::jump, {}, {}, {"restore"}, {}});
  raz::compiler::MirBlock projection_restore{"restore", {}};
  projection_restore.instructions.push_back({raz::compiler::MirOpcode::constant, "replacement", "i64", {"9"}, {}});
  projection_restore.instructions.push_back({raz::compiler::MirOpcode::store, {}, "i64", {"replacement", "left"}, {}});
  projection_restore.instructions.push_back({raz::compiler::MirOpcode::load, "restored", "i64", {"left"}, {}});
  projection_restore.instructions.push_back({raz::compiler::MirOpcode::return_value, {}, "i64", {"restored"}, {}});
  projection_reinit_function.blocks.push_back(std::move(projection_reinit_entry));
  projection_reinit_function.blocks.push_back(std::move(projection_restore));
  projection_reinit.functions.push_back(std::move(projection_reinit_function));
  if (!projection_reinit.verify().empty()) {
    std::cerr << "verifier rejected projection reinitialization after move\n";
    return EXIT_FAILURE;
  }

  // CFG joins preserve projection granularity: one branch may move `left`
  // without invalidating the disjoint `right` field at the merge.
  raz::compiler::MirModule cfg_partial_projection;
  raz::compiler::MirFunction cfg_partial_function;
  cfg_partial_function.name = "cfg_partial_projection";
  cfg_partial_function.return_type = "i64";
  raz::compiler::MirBlock cfg_partial_entry{"entry", {}};
  cfg_partial_entry.instructions.push_back({raz::compiler::MirOpcode::stack_allocate, "pair", "Pair", {"16", "8"}, {}});
  cfg_partial_entry.instructions.push_back({raz::compiler::MirOpcode::storage_live, {}, "Pair", {"pair"}, {}});
  cfg_partial_entry.instructions.push_back({raz::compiler::MirOpcode::place_path, {}, "Pair", {"pair", "value"}, {}});
  cfg_partial_entry.instructions.push_back({raz::compiler::MirOpcode::pointer_offset, "left", "i64", {"pair", "0"}, {}});
  cfg_partial_entry.instructions.push_back({raz::compiler::MirOpcode::place_path, {}, "i64", {"left", "value.left"}, {}});
  cfg_partial_entry.instructions.push_back({raz::compiler::MirOpcode::pointer_offset, "right", "i64", {"pair", "8"}, {}});
  cfg_partial_entry.instructions.push_back({raz::compiler::MirOpcode::place_path, {}, "i64", {"right", "value.right"}, {}});
  cfg_partial_entry.instructions.push_back({raz::compiler::MirOpcode::constant, "condition", "bool", {"true"}, {}});
  cfg_partial_entry.instructions.push_back({raz::compiler::MirOpcode::branch, {}, "bool", {"condition", "moved", "kept"}, {}});
  raz::compiler::MirBlock cfg_partial_moved{"moved", {}};
  cfg_partial_moved.instructions.push_back({raz::compiler::MirOpcode::move_value, {}, "i64", {"left", "value.left"}, {}});
  cfg_partial_moved.instructions.push_back({raz::compiler::MirOpcode::jump, {}, {}, {"merge"}, {}});
  raz::compiler::MirBlock cfg_partial_kept{"kept", {}};
  cfg_partial_kept.instructions.push_back({raz::compiler::MirOpcode::jump, {}, {}, {"merge"}, {}});
  raz::compiler::MirBlock cfg_partial_merge{"merge", {}};
  cfg_partial_merge.instructions.push_back({raz::compiler::MirOpcode::load, "result", "i64", {"right"}, {}});
  cfg_partial_merge.instructions.push_back({raz::compiler::MirOpcode::return_value, {}, "i64", {"result"}, {}});
  cfg_partial_function.blocks.push_back(std::move(cfg_partial_entry));
  cfg_partial_function.blocks.push_back(std::move(cfg_partial_moved));
  cfg_partial_function.blocks.push_back(std::move(cfg_partial_kept));
  cfg_partial_function.blocks.push_back(std::move(cfg_partial_merge));
  cfg_partial_projection.functions.push_back(std::move(cfg_partial_function));
  if (!cfg_partial_projection.verify().empty()) {
    std::cerr << "verifier rejected disjoint field at CFG join after partial move\n";
    return EXIT_FAILURE;
  }

  // Constant indexes are disjoint, but a dynamic index conservatively aliases all elements.
  const auto make_index_module = [](bool dynamic_use) {
    raz::compiler::MirModule indexed;
    raz::compiler::MirFunction indexed_function;
    indexed_function.name = dynamic_use ? "dynamic_index_overlap" : "constant_index_disjoint";
    indexed_function.return_type = "i64";
    raz::compiler::MirBlock indexed_entry{"entry", {}};
    indexed_entry.instructions.push_back({raz::compiler::MirOpcode::stack_allocate, "items", "[2]i64", {"16", "8"}, {}});
    indexed_entry.instructions.push_back({raz::compiler::MirOpcode::storage_live, {}, "[2]i64", {"items"}, {}});
    indexed_entry.instructions.push_back({raz::compiler::MirOpcode::place_path, {}, "[2]i64", {"items", "items"}, {}});
    indexed_entry.instructions.push_back({raz::compiler::MirOpcode::pointer_offset, "first", "i64", {"items", "0"}, {}});
    indexed_entry.instructions.push_back({raz::compiler::MirOpcode::place_path, {}, "i64", {"first", "items[0]"}, {}});
    indexed_entry.instructions.push_back({raz::compiler::MirOpcode::pointer_offset, "other", "i64", {"items", "8"}, {}});
    indexed_entry.instructions.push_back({raz::compiler::MirOpcode::place_path, {}, "i64", {"other", dynamic_use ? "items[*]" : "items[1]"}, {}});
    indexed_entry.instructions.push_back({raz::compiler::MirOpcode::move_value, {}, "i64", {"first", "items[0]"}, {}});
    indexed_entry.instructions.push_back({raz::compiler::MirOpcode::jump, {}, {}, {"after"}, {}});
    raz::compiler::MirBlock indexed_after{"after", {}};
    indexed_after.instructions.push_back({raz::compiler::MirOpcode::load, "result", "i64", {"other"}, {}});
    indexed_after.instructions.push_back({raz::compiler::MirOpcode::return_value, {}, "i64", {"result"}, {}});
    indexed_function.blocks.push_back(std::move(indexed_entry));
    indexed_function.blocks.push_back(std::move(indexed_after));
    indexed.functions.push_back(std::move(indexed_function));
    return indexed;
  };
  if (!make_index_module(false).verify().empty()) {
    std::cerr << "verifier rejected disjoint constant-index use after move\n";
    return EXIT_FAILURE;
  }

  if (make_index_module(true).verify().empty()) {
    std::cerr << "verifier accepted dynamic-index alias after element move\n";
    return EXIT_FAILURE;
  }

  raz::compiler::MirModule resumable_module;
  resumable_module.functions.push_back(std::move(resumable));
  resumable_module.functions.push_back(std::move(cfg_async));
  resumable_module.functions.push_back(std::move(allocated_cfg));
  if (!resumable_module.verify().empty()) {
    std::cerr << "verifier rejected analyzed async frame\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
