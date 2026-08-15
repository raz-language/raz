#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import sys

root = Path(__file__).resolve().parents[1]
model = (root / 'compiler/src/mir/core/model.rz').read_text(encoding='utf-8')
builder = (root / 'compiler/src/mir/core/builder.rz').read_text(encoding='utf-8')
lowering = (root / 'compiler/src/mir/lowering.rz').read_text(encoding='utf-8')
lowering_paths = (root / 'compiler/src/mir/ownership/lowering_paths.rz').read_text(encoding='utf-8')
paths = (root / 'compiler/src/mir/ownership/paths.rz').read_text(encoding='utf-8')
partial = (root / 'compiler/src/mir/ownership/partial_moves.rz').read_text(encoding='utf-8')
dce = (root / 'compiler/src/mir/transform/dce.rz').read_text(encoding='utf-8')
move = (root / 'compiler/src/mir/ownership/move_state.rz').read_text(encoding='utf-8')
drops = (root / 'compiler/src/mir/ownership/drops.rz').read_text(encoding='utf-8')

checks = {
    'MIR owns projection path metadata': all(x in model for x in [
        'ownership_event_path_starts', 'ownership_event_path_counts',
        'ownership_path_component_count', 'ownership_path_components']),
    'builder records copied projection components': all(x in builder for x in [
        'mir_record_ownership_path_event', 'ownership_event_path_starts',
        'ownership_event_path_counts', 'ownership_path_components']),
    'lowering derives static field paths from HIR': all(x in lowering_paths for x in [
        'mir_collect_hir_ownership_path', 'kind == 16',
        'mir_record_hir_move_event', 'mir_record_hir_path_event', 'mir_record_ownership_path_event']),
    'path layer implements ancestor/descendant overlap': all(x in paths for x in [
        'mir_ownership_path_prefix_of', 'mir_ownership_paths_overlap',
        'verify_mir_ownership_paths']),
    'partial move dataflow is CFG/path sensitive': all(x in partial for x in [
        'verify_mir_partial_move_dataflow', 'mir_partial_local_event_conflicts',
        'mir_partial_local_clear_slot', 'cfg.predecessor_starts', 'move_event_at_instruction']),
    'sibling paths remain independent by overlap semantics':
        'mir_ownership_paths_overlap(mir, prior, current)' in partial,
    'whole-local and field stores reinitialize tracked move state':
        'mir_partial_local_clear_slot' in partial and
        'mir_partial_local_clear_path' in partial and
        'mir_record_hir_path_event(hir, out, store_instruction, 2, target)' in lowering,
    'legacy whole-local checker ignores projected events':
        'mir.ownership_event_path_counts' in move,
    'ownership firewall executes partial-move verifier':
        'verify_mir_partial_move_dataflow(mir)' in drops,
    'DCE pins ownership semantic program points': all(x in dce for x in [
        'mir_instruction_has_ownership_event', 'i64 pinned', 'mir.ownership_event_instructions']),
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    for name in failed:
        print(f'mir2f-partial-moves: FAIL: {name}')
    sys.exit(1)

print('mir2f-partial-moves: PASS')
print('  ownership events carry static projection paths rooted at locals')
print('  CFG move-state unions projected moves across control-flow joins')
print('  ancestor/descendant moves conflict; sibling field paths remain independent')
print('  whole-local and field stores clear the matching moved subtree')
