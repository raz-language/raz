#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import re
import sys
sys.dont_write_bytecode = True

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / "tools"))
from compiler_sources import combined_source
source = combined_source(root)

name = 'fn emit_compiler_runtime_declarations'
start = source.find(name)
if start < 0:
    raise SystemExit('compiler-runtime-declarations: FAIL: runtime declaration emitter not found')
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
    raise SystemExit('compiler-runtime-declarations: FAIL: unterminated runtime declaration emitter')
block = source[brace + 1:end - 1]
encoded = bytes(int(value) for value in re.findall(r'writer_put\(out,\s*(\d+)\);', block)).decode('ascii')

required = {
    'raz_compiler_rt_arena_create',
    'raz_compiler_rt_arena_destroy',
    'raz_compiler_rt_arena_get',
    'raz_compiler_rt_arena_set',
    'raz_compiler_rt_arena_copy',
    'raz_compiler_rt_ref_create',
    'raz_compiler_rt_ref_get',
    'raz_compiler_rt_ref_set',
}
# The canonical compiler is flattened into one compiler translation unit during
# recursive bootstrap. Repeating an external function declaration in two Raz
# modules therefore becomes a semantic duplicate-name error in production compiler. Keep
# the ABI boundary single-owned and catch accidental backend-local redeclarations
# before bootstrap.
extern_names = re.findall(r'\bextern\s+fn\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(', source)
seen = set()
duplicate_externs = sorted({symbol for symbol in extern_names if symbol in seen or seen.add(symbol)})
if duplicate_externs:
    print('compiler-runtime-declarations: FAIL')
    for symbol in duplicate_externs:
        print(f'  duplicate compiler extern declaration: {symbol}')
    sys.exit(1)

missing = sorted(symbol for symbol in required if f'@{symbol}' not in encoded)
if missing:
    print('compiler-runtime-declarations: FAIL')
    for symbol in missing:
        print(f'  missing Forge extern declaration: {symbol}')
    sys.exit(1)
print(f'compiler-runtime-declarations: PASS ({len(required)} required core runtime declarations)')
