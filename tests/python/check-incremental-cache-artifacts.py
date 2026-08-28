#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import sys

root = Path(__file__).resolve().parents[2]
inc = (root / 'compiler/src/raz_driver/src/incremental.rz').read_text()
model = (root / 'compiler/src/raz_hir/src/hir/core/model.rz').read_text()
main = (root / 'compiler/src/raz_driver/src/compiler_main.rz').read_text()

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
        'writer_literal(&mut writer, "RAZMIR 2\\n");' in inc and
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
    'artifact cache persists semantic source and backend option fingerprints':
        '/artifact.semantic.key' in inc and '/artifact.options.key' in inc and
        'fn incremental_semantic_source_hash(' in inc and
        'fn incremental_artifact_options_key(' in inc,
    'cached native artifact has independent size/content integrity':
        '/artifact.integrity' in inc and 'fn incremental_artifact_integrity_matches(' in inc and
        'incremental_input_content_hash(artifact_path, artifact_length, expected_size)' in inc,
    'semantic fast path preserves quoted literals and skips comments only outside them':
        'Quoted literals are copied byte-for-byte' in inc and
        'line_comment' in inc and 'block_comment' in inc and 'pending_separator' in inc,
    'frontend semantic artifact has a dedicated cache file':
        '/frontend.state' in inc and
        'incremental_cache_path(manifest_path, manifest_length, 10, state_path, 8192)' in inc,
    'incremental check restores frontend artifact before legacy debug state':
        'Pass 60 gives frontend semantic state its own durable artifact' in inc and
        inc.index('incremental_cache_path(manifest_path, manifest_length, 10, state_path, 8192)') <
        inc.index('state_length = incremental_cache_path(manifest_path, manifest_length, 2, state_path, 8192)', inc.index('fn incremental_load_check_hints(')),
    'executable MIR units have a dedicated cache artifact':
        '/mir.units' in inc and 'fn incremental_persist_mir_units(' in inc and
        'incremental_persist_mir_units(' in main,
    'MIR unit image records executable lanes and ownership identity':
        'RAZMIRUNITS 2\\n' in inc and
        all(token in inc for token in ['mir.function_count', 'mir.instruction_count', 'mir.global_count',
                                       'mir.call_argument_count', 'mir.function_module_ids', 'mir.opcodes',
                                       'mir.op_types', 'mir.op_a', 'mir.op_b', 'mir.op_c', 'mir.op_d']),
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
