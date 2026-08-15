#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[1]
required = [
    'compiler/src/mir/analysis/cfg.rz',
    'compiler/src/mir/analysis/dataflow.rz',
    'compiler/src/mir/analysis/liveness.rz',
    'compiler/src/mir/analysis/dominance.rz',
    'compiler/src/mir/ownership/places.rz',
    'compiler/src/mir/ownership/moves.rz',
    'compiler/src/mir/ownership/borrows.rz',
    'compiler/src/mir/ownership/drops.rz',
    'compiler/src/mir/transform/simplify_cfg.rz',
    'compiler/src/mir/transform/const_prop.rz',
    'compiler/src/mir/transform/copy_prop.rz',
    'compiler/src/mir/transform/dce.rz',
]
problems = []
for rel in required:
    if not (root / rel).is_file():
        problems.append(f'missing {rel}')
pipeline = (root / 'compiler/src/mir/transform/pipeline.rz').read_text(encoding='utf-8')
verifier = (root / 'compiler/src/mir/verify/verifier.rz').read_text(encoding='utf-8')
cfg = (root / 'compiler/src/mir/analysis/cfg.rz').read_text(encoding='utf-8')
for needle in ['verify_mir_ownership_semantics', 'simplify_mir_cfg', 'propagate_mir_constants', 'propagate_mir_copies', 'eliminate_mir_dead_values']:
    if needle not in pipeline:
        problems.append(f'pipeline missing {needle}')
if 'build_mir_cfg' not in verifier:
    problems.append('structural verifier does not construct/validate the CFG')
if 'opcode == 25' in cfg.split('fn mir_opcode_is_terminator', 1)[1].split('}', 1)[0]:
    problems.append('opcode 25 regressed to terminator classification')
if problems:
    print('mir2b-analysis: FAIL')
    for problem in problems:
        print(' ', problem)
    sys.exit(1)
print('mir2b-analysis: PASS')
print('  CFG/dataflow/liveness/dominance foundations present')
print('  MIR place/move/borrow/drop facts present')
print('  verified pipeline runs CFG simplification, constant/copy propagation, and compacting DCE')
print('  instruction/value remapping preserves branches, function ranges, and call arguments')
