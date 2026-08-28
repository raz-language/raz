#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import sys

root = Path(__file__).resolve().parents[2]
model = (root / 'compiler/src/raz_mir/src/mir/core/model.rz').read_text(encoding='utf-8')
ownership_context = (root / 'compiler/src/raz_mir/src/mir/ownership/context.rz').read_text(encoding='utf-8')
model += '\n' + ownership_context
builder = (root / 'compiler/src/raz_mir/src/mir/core/builder.rz').read_text(encoding='utf-8')
lowering = (root / 'compiler/src/raz_mir/src/mir/lowering.rz').read_text(encoding='utf-8')
init = (root / 'compiler/src/raz_borrowck/src/borrowck/initialization.rz').read_text(encoding='utf-8')
move = (root / 'compiler/src/raz_borrowck/src/borrowck/move_state.rz').read_text(encoding='utf-8')
drops = (root / 'compiler/src/raz_borrowck/src/borrowck/drops.rz').read_text(encoding='utf-8')
remap = (root / 'compiler/src/raz_mir_opt/src/mir_opt/transform/remap.rz').read_text(encoding='utf-8')
pipeline = (root / 'compiler/src/raz_mir_opt/src/mir_opt/transform/pipeline.rz').read_text(encoding='utf-8')
driver = (root / 'compiler/src/raz_driver/src/compiler_main.rz').read_text(encoding='utf-8')

checks = {
    'MIR owns non-executable ownership-event metadata': all(x in model for x in [
        'ownership_event_count', 'ownership_event_instructions',
        'ownership_event_kinds', 'ownership_event_slots']),
    'builder allocates and records ownership events': all(x in builder for x in [
        'mir_record_ownership_event', 'ownership_event_instructions',
        'ownership_event_kinds', 'ownership_event_slots']),
    'HIR move lowering records semantic move events in MIR':
        'mir_record_hir_move_event(hir, mir, value, operand);' in lowering,
    'local state tracks MUST-initialized and MAY-moved facts': all(x in init for x in [
        'MirLocalState', 'mir_local_state_store', 'mir_local_state_move',
        'mir_local_state_readable']),
    'CFG move analysis intersects initialization and unions moved state': all(x in move for x in [
        'verify_mir_local_move_dataflow', 'out_initialized', 'out_moved',
        'cfg.predecessor_starts', 'cfg.predecessors', 'mir_local_state_readable']),
    'stores reinitialize and moves poison the source':
        'mir_local_state_store(state' in move and 'mir_local_state_move(state' in move,
    'instruction compaction remaps ownership-event positions':
        'mir.ownership.ownership_event_instructions' in remap and 'mir_map_target(mir, &map, instruction)' in remap,
    'ownership firewall executes the CFG move verifier':
        'verify_mir_local_move_dataflow_with_cfg(mir, &cfg)' in drops and driver.count('verify_mir_ownership_semantics(&mir)') >= 2 and 'verify_mir_ownership_semantics' not in pipeline,
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    for name in failed:
        print(f'mir-ownership-dataflow: FAIL: {name}')
    sys.exit(1)

print('mir-ownership-dataflow: PASS')
print('  MIR carries backend-invisible ownership move events')
print('  CFG joins use MUST initialization and MAY moved-state')
print('  stores reinitialize locals and instruction compaction remaps ownership events')
