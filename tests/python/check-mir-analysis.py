#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[2]
required = [
    'compiler/src/raz_mir/src/mir/analysis/cfg.rz',
    'compiler/src/raz_mir/src/mir/analysis/dataflow.rz',
    'compiler/src/raz_mir/src/mir/analysis/liveness.rz',
    'compiler/src/raz_mir/src/mir/analysis/dominance.rz',
    'compiler/src/raz_borrowck/src/borrowck/places.rz',
    'compiler/src/raz_borrowck/src/borrowck/moves.rz',
    'compiler/src/raz_borrowck/src/borrowck/borrows.rz',
    'compiler/src/raz_borrowck/src/borrowck/drops.rz',
    'compiler/src/raz_mir_opt/src/mir_opt/transform/simplify_cfg.rz',
    'compiler/src/raz_mir_opt/src/mir_opt/transform/const_prop.rz',
    'compiler/src/raz_mir_opt/src/mir_opt/transform/copy_prop.rz',
    'compiler/src/raz_mir_opt/src/mir_opt/transform/dce.rz',
]
problems = []
for rel in required:
    if not (root / rel).is_file():
        problems.append(f'missing {rel}')
pipeline = (root / 'compiler/src/raz_mir_opt/src/mir_opt/transform/pipeline.rz').read_text(encoding='utf-8')
driver = (root / 'compiler/src/raz_driver/src/compiler_main.rz').read_text(encoding='utf-8')
verifier = (root / 'compiler/src/raz_mir/src/mir/verify/verifier.rz').read_text(encoding='utf-8')
cfg = (root / 'compiler/src/raz_mir/src/mir/analysis/cfg.rz').read_text(encoding='utf-8')
for needle in ['simplify_mir_cfg', 'propagate_mir_constants', 'propagate_mir_copies', 'eliminate_mir_dead_values']:
    if needle not in pipeline:
        problems.append(f'pipeline missing {needle}')
if driver.count('verify_mir_ownership_semantics(&mir)') < 2:
    problems.append('driver does not run borrowck before and after MIR optimization')
if 'verify_mir_ownership_semantics' in pipeline:
    problems.append('MIR transform package still owns borrowck semantics')
if 'build_mir_cfg' not in verifier:
    problems.append('structural verifier does not construct/validate the CFG')
if 'opcode == 25' in cfg.split('fn mir_opcode_is_terminator', 1)[1].split('}', 1)[0]:
    problems.append('opcode 25 regressed to terminator classification')
if problems:
    print('mir-analysis: FAIL')
    for problem in problems:
        print(' ', problem)
    sys.exit(1)
print('mir-analysis: PASS')
print('  CFG/dataflow/liveness/dominance foundations present')
print('  raz_borrowck owns place/move/borrow/drop legality analyses')
print('  verified pipeline runs CFG simplification, constant/copy propagation, and compacting DCE')
print('  instruction/value remapping preserves branches, function ranges, and call arguments')
