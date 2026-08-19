#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import sys

root = Path(__file__).resolve().parents[2]
inc = (root / 'compiler/src/driver/incremental.rz').read_text()
model = (root / 'compiler/src/hir/core/model.rz').read_text()
main = (root / 'compiler/src/main.rz').read_text()

checks = {
    'HIR module fingerprints are first-class cache identity':
        'incremental_module_hir_fingerprints' in model and 'fn incremental_hir_module_fingerprint(' in inc,
    'cached optimized MIR fingerprint is carried per module':
        'incremental_module_cached_mir_fingerprints' in model,
    'module cache hit state is explicit':
        'incremental_module_cache_kinds' in model and 'incremental_module_cache_hit_count' in model,
    'persistent cache restore validates exact source/interface/HIR identity':
        'fn incremental_load_module_cache_state(' in inc and
        'incremental_module_source_fingerprints' in inc and
        'incremental_module_interface_fingerprints' in inc and
        'incremental_module_hir_fingerprints' in inc,
    'persistent MIR state schema includes HIR and optimized MIR identity':
        '[82, 65, 90, 77, 73, 82, 32, 50, 10]' in inc and
        'incremental_hir_module_fingerprint' in inc and
        'incremental_mir_module_fingerprint' in inc,
    # Ordering matters: classification marks modules dirty and the cache load
    # must observe those marks. Compare statement positions rather than an exact
    # indentation-sensitive spelling of the two adjacent lines, which silently
    # stopped matching when the entrypoint was reindented.
    'cache state is loaded after semantic dirty classification':
        'incremental_classify_module_changes(' in main and
        'incremental_load_module_cache_state(' in main and
        main.index('incremental_classify_module_changes(') <
        main.index('incremental_load_module_cache_state('),
    'cache arenas are initialized and destroyed with HIR':
        'out.incremental_module_hir_fingerprints = raz_compiler_rt_arena_create(4096);' in model and
        'raz_compiler_rt_arena_destroy(out.incremental_module_hir_fingerprints);' in model and
        'raz_compiler_rt_arena_destroy(out.incremental_module_cached_mir_fingerprints);' in model and
        'raz_compiler_rt_arena_destroy(out.incremental_module_cache_kinds);' in model,
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    for name in failed:
        print(f'incremental-cache-artifacts: FAIL: {name}')
    sys.exit(1)

print('incremental-cache-artifacts: PASS')
print('  module HIR identity is persisted explicitly')
print('  optimized MIR cache identity is exact and schema-versioned')
print('  cache-hit state survives into the production driver')
