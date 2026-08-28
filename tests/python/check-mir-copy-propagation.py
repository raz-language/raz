#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
copy_prop = (ROOT / 'compiler/src/raz_mir_opt/src/mir_opt/transform/copy_prop.rz').read_text(encoding='utf-8')
remap = (ROOT / 'compiler/src/raz_mir_opt/src/mir_opt/transform/remap.rz').read_text(encoding='utf-8')
writer = (ROOT / 'compiler/src/raz_codegen_forge/src/forge/writer.rz').read_text(encoding='utf-8')

checks = {
    'copy propagation collects a replacement table': 'i64 replacements = raz_compiler_rt_arena_create(mir.instruction_count + 1);' in copy_prop,
    'copy propagation does not rescan MIR per copy': 'mir_replace_value_uses(mir, ip, source)' not in copy_prop,
    'copy propagation applies replacements once': 'mir_apply_value_replacements(mir, replacements)' in copy_prop,
    'replacement chains are resolved transitively': 'mir_resolve_value_replacement' in remap and 'while (steps < instruction_count)' in remap,
    'MIR operand helpers use bounded fast arena access': 'raz_compiler_rt_arena_get_unchecked(mir.op_a, ip)' in remap and 'raz_compiler_rt_arena_set_unchecked(mir.op_a, ip, value)' in remap,
    'Forge writer byte emission uses bounded fast arena access': 'raz_compiler_rt_arena_set_unchecked(out.data, out.length, code);' in writer,
    'Forge writer keeps explicit capacity guard': 'out.length >= out.capacity && !writer_grow(out)' in writer,
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    for name in failed:
        print(f'FAIL: {name}')
    raise SystemExit(1)
print(f'MIR copy/Forge writer fast paths: PASS ({len(checks)} checks)')
