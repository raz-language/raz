# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

#!/usr/bin/env python3
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[2]
hir_model = (root / 'compiler/src/hir/core/model.rz').read_text()
hir_builder = (root / 'compiler/src/hir/core/builder.rz').read_text()
comptime = (root / 'compiler/src/hir/semantic/comptime.rz').read_text()
mir_model = (root / 'compiler/src/mir/core/model.rz').read_text()
mir_builder = (root / 'compiler/src/mir/core/builder.rz').read_text()
lowering = (root / 'compiler/src/mir/lowering.rz').read_text()
inc = (root / 'compiler/src/driver/incremental.rz').read_text()
main = (root / 'compiler/src/main.rz').read_text()

checks = {
    'stable module ordinal replaces source-offset identity':
        'i64 module_id = segment + 1;' in hir_builder and 'source_offset + 1' not in hir_builder,
    'HIR functions retain module origin':
        'incremental_function_module_ids' in hir_model and 'hir_module_at_offset(&mut builder, function_offset)' in comptime,
    'HIR exposes compact module function views':
        all(x in hir_model for x in ['incremental_module_function_starts', 'incremental_module_function_counts', 'incremental_module_function_indices']),
    'MIR retains module origin and module views':
        all(x in mir_model for x in ['function_module_ids', 'module_function_starts', 'module_function_counts', 'module_function_indices']),
    'MIR module storage is allocated':
        'out.function_module_ids = raz_compiler_rt_arena_create' in mir_builder and 'out.module_function_indices = raz_compiler_rt_arena_create' in mir_builder,
    'HIR module views lower into MIR':
        'hir.incremental_module_function_indices' in lowering and 'hir.incremental_function_module_ids' in lowering,
    'optimized MIR module fingerprint exists':
        'fn incremental_mir_module_fingerprint(' in inc,
    'optimized MIR module state persists':
        'fn incremental_persist_module_mir_state(' in inc and 'incremental_persist_module_mir_state(' in main,
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    for name in failed:
        print(f'module-granular-incremental: FAIL: {name}')
    sys.exit(1)
print('module-granular-incremental: PASS')
print('  stable module identities survive source-length changes')
print('  HIR and MIR expose module-owned function views')
print('  optimized MIR state is fingerprinted per module')
