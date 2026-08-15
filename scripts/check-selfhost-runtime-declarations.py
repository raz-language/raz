#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import re
import sys
sys.dont_write_bytecode = True

root = Path(__file__).resolve().parents[1]
from selfhost_sources import combined_source
source = combined_source(root)

name = 'fn emit_stage1_runtime_declarations'
start = source.find(name)
if start < 0:
    raise SystemExit('selfhost-runtime-declarations: FAIL: runtime declaration emitter not found')
brace = source.find('{', start)
depth = 1
end = brace + 1
while end < len(source) and depth:
    if source[end] == '{':
        depth += 1
    elif source[end] == '}':
        depth -= 1
    end += 1
if depth:
    raise SystemExit('selfhost-runtime-declarations: FAIL: unterminated runtime declaration emitter')
block = source[brace + 1:end - 1]
encoded = bytes(int(value) for value in re.findall(r'writer_put\(out,\s*(\d+)\);', block)).decode('ascii')

required = {
    'raz_rt_stage1_arena_create',
    'raz_rt_stage1_arena_destroy',
    'raz_rt_stage1_arena_get',
    'raz_rt_stage1_arena_set',
    'raz_rt_stage1_arena_copy',
    'raz_rt_stage1_ref_create',
    'raz_rt_stage1_ref_get',
    'raz_rt_stage1_ref_set',
}
# The canonical compiler is flattened into one self-host translation unit during
# recursive bootstrap. Repeating an external function declaration in two Raz
# modules therefore becomes a semantic duplicate-name error in Stage 2+. Keep
# the ABI boundary single-owned and catch accidental backend-local redeclarations
# before bootstrap.
extern_names = re.findall(r'\bextern\s+fn\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(', source)
seen = set()
duplicate_externs = sorted({symbol for symbol in extern_names if symbol in seen or seen.add(symbol)})
if duplicate_externs:
    print('selfhost-runtime-declarations: FAIL')
    for symbol in duplicate_externs:
        print(f'  duplicate compiler extern declaration: {symbol}')
    sys.exit(1)

missing = sorted(symbol for symbol in required if f'@{symbol}' not in encoded)
if missing:
    print('selfhost-runtime-declarations: FAIL')
    for symbol in missing:
        print(f'  missing Forge extern declaration: {symbol}')
    sys.exit(1)
print(f'selfhost-runtime-declarations: PASS ({len(required)} required core runtime declarations)')
