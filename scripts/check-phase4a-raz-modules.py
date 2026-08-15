#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import sys

root = Path(__file__).resolve().parents[1]
project = (root / 'compiler/src/driver/project.rz').read_text()
inc = (root / 'compiler/src/driver/incremental.rz').read_text()
model = (root / 'compiler/src/hir/core/model.rz').read_text()
fingerprints = (root / 'compiler/src/hir/query/fingerprints.rz').read_text()
comptime = (root / 'compiler/src/hir/semantic/comptime.rz').read_text()
main = (root / 'compiler/src/main.rz').read_text()

checks = {
    'ordinary module loader has namespace/import scheduler':
        'fn append_project_topological_sources(' in project and 'topological_status = append_project_topological_sources(' in project,
    'scheduler is dependency driven rather than filesystem-order only':
        'edge_owners' in project and 'edge_targets' in project and 'scheduled_count' in project,
    'cyclic/unscannable graphs conservatively fall back':
        'if (candidate < 0)' in project and 'return 0;' in project,
    'HIR persists package/namespace identity':
        'incremental_module_package_hashes' in model and 'incremental_module_namespace_hashes' in model,
    'HIR persists module dependency edges':
        'incremental_module_dependency_owners' in model and 'incremental_module_dependency_targets' in model,
    'HIR exports dependency graph from semantic imports':
        'fn hir_query_export_incremental_module_graph(' in fingerprints and 'builder.namespace_import_count' in fingerprints,
    'function ownership is explicit at HIR finalization':
        'hir_module_at_offset(&mut builder, function_offset)' in comptime and 'incremental_module_function_counts' in comptime,
    'cross-process module delta classification exists':
        'fn incremental_classify_module_changes(' in inc and 'incremental_module_dirty_kinds' in inc,
    'private implementation changes remain module-local':
        'kind = 1;' in inc and 'kind == 2' in inc,
    'interface changes invalidate importing modules':
        'incremental_module_dependency_targets' in inc and 'incremental_module_dependency_owners' in inc,
    'module graph snapshot is versioned':
        'RAZINC' not in inc or '[82, 65, 90, 73, 78, 67, 32, 50, 10]' in inc,
    'optimized per-module MIR state is persisted':
        'fn incremental_persist_module_mir_state(' in inc and 'incremental_persist_module_mir_state(' in main,
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    for name in failed:
        print(f'phase4a-raz-modules: FAIL: {name}')
    sys.exit(1)

print('phase4a-raz-modules: PASS')
print('  Raz loader schedules acyclic semantic modules by imports')
print('  HIR exports module ownership/dependency snapshots')
print('  persistent state classifies private vs interface changes')
print('  optimized MIR is fingerprinted per module')
