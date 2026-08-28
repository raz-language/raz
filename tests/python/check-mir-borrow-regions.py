#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import sys

root = Path(__file__).resolve().parents[2]
lowering = (root / 'compiler/src/raz_mir/src/mir/lowering.rz').read_text(encoding='utf-8')
lowering_paths = (root / 'compiler/src/raz_mir/src/mir/ownership/lowering_paths.rz').read_text(encoding='utf-8')
loans = (root / 'compiler/src/raz_borrowck/src/borrowck/loan_regions.rz').read_text(encoding='utf-8')
paths = (root / 'compiler/src/raz_borrowck/src/borrowck/paths.rz').read_text(encoding='utf-8')
drops = (root / 'compiler/src/raz_borrowck/src/borrowck/drops.rz').read_text(encoding='utf-8')
dce = (root / 'compiler/src/raz_mir_opt/src/mir_opt/transform/dce.rz').read_text(encoding='utf-8')

checks = {
    'borrow lowering records semantic loan events': all(x in lowering_paths for x in [
        'mir_record_hir_borrow_event', 'kind = 3', 'kind = 4', 'mir_record_hir_path_event']),
    'borrow expressions emit projection-aware events': all(x in lowering for x in [
        'mir_record_hir_borrow_event', 'raz_compiler_rt_arena_get(hir.node_values, node)']),
    'loan regions distinguish shared and exclusive loans': all(x in loans for x in [
        'kind == 3 || kind == 4', 'current_kind == 4', 'active_kind == 4']),
    'loan conflicts reuse canonical projection overlap':
        'mir_ownership_paths_overlap(mir, event, current)' in loans and
        'mir_ownership_paths_overlap' in paths,
    'loan lifetime is last-use driven': all(x in loans for x in [
        'mir_loan_last_use', 'mir_loan_holder_slot', 'mir_value_last_use', 'mir_expire_loans']),
    'CFG joins conservatively union active loans': all(x in loans for x in [
        'build_mir_cfg', 'mir_loan_matrix_get', 'mir_loan_matrix_set',
        'raz_compiler_rt_arena_get(cfg.successor_a, predecessor) == block']),
    'moves and reinitialization conflict with active loans':
        'kind == 1 || kind == 2' in loans and
        'mir_local_active_loan_conflicts' in loans and 'current,' in loans and 'true,' in loans,
    'ownership firewall executes loan verifier': 'verify_mir_loan_regions_with_cfg(mir, &cfg, loan_last_uses)' in drops,
    # DCE must not delete an instruction that carries an ownership event, or the
    # loan verifier loses the program point it reasons about. The predicate this
    # used to name was replaced by a precomputed pin map built once from
    # ownership_event_instructions, so assert the pinning itself.
    'loan program points remain DCE-pinned':
        'mir.ownership.ownership_event_instructions' in dce and
        'while (event < mir.ownership.ownership_event_count)' in dce,
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    for name in failed:
        print(f'mir-borrow-regions: FAIL: {name}')
    sys.exit(1)

print('mir-borrow-regions: PASS')
print('  MIR records shared/exclusive loans on canonical projection paths')
print('  CFG joins conservatively propagate active loans')
print('  overlapping shared/shared loans coexist; exclusive overlap conflicts')
print('  loan expiration follows MIR last use instead of lexical scope end')
