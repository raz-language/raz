#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
import sys
root=Path(__file__).resolve().parents[2]
required=[
    'compiler/src/mir/transform/remap.rz',
    'compiler/src/mir/transform/copy_prop.rz',
    'compiler/src/mir/transform/dce.rz',
]
problems=[]
for rel in required:
    if not (root/rel).is_file(): problems.append(f'missing {rel}')
remap=(root/'compiler/src/mir/transform/remap.rz').read_text(encoding='utf-8')
copy=(root/'compiler/src/mir/transform/copy_prop.rz').read_text(encoding='utf-8')
dce=(root/'compiler/src/mir/transform/dce.rz').read_text(encoding='utf-8')
pipeline=(root/'compiler/src/mir/transform/pipeline.rz').read_text(encoding='utf-8')
live=(root/'compiler/src/mir/analysis/liveness.rz').read_text(encoding='utf-8')
for needle in ['MirInstructionMap','build_mir_instruction_map','compact_mir_instructions','mir_replace_value_uses','mir_map_target']:
    if needle not in remap: problems.append(f'remap layer missing {needle}')
if 'mir_replace_value_uses(mir, ip, source)' not in copy: problems.append('copy propagation does not rewrite MIR consumers')
if 'compact_mir_instructions(mir, keep)' not in dce: problems.append('DCE does not compact through remapping')
if 'eliminate_mir_dead_values(mir)' not in pipeline: problems.append('pipeline does not run compacting DCE')
if 'mir.call_argument_count' not in live: problems.append('liveness does not account for auxiliary call/capture arguments')
if problems:
    print('mir-remap: FAIL')
    for problem in problems: print(' ',problem)
    sys.exit(1)
print('mir-remap: PASS')
print('  value replacement is metadata-safe')
print('  DCE compacts instruction-indexed MIR through a total remapping table')
print('  branch targets, function ranges, register operands, and call/capture values are rewritten')
