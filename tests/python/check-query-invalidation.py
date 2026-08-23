#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import sys

root = Path(__file__).resolve().parents[2]
model = (root / 'compiler/src/hir/core/model.rz').read_text(encoding='utf-8')
builder = (root / 'compiler/src/hir/core/builder.rz').read_text(encoding='utf-8')
engine = (root / 'compiler/src/hir/query/engine.rz').read_text(encoding='utf-8')
invalidation = (root / 'compiler/src/hir/query/invalidation.rz').read_text(encoding='utf-8')
fingerprints = (root / 'compiler/src/hir/query/fingerprints.rz').read_text(encoding='utf-8')
comptime = (root / 'compiler/src/hir/semantic/comptime.rz').read_text(encoding='utf-8')
order = {path.relative_to(root / 'compiler').as_posix() for path in (root / 'compiler/src').rglob('*.rz')}

checks = {
    'semantic input families have independent fingerprints': all(x in model + invalidation for x in [
        'query_input_traits_fingerprint', 'query_input_methods_fingerprint',
        'query_input_aggregates_fingerprint', 'query_input_generics_fingerprint',
        'query_input_functions_fingerprint', 'query_input_associated_fingerprint']),
    'cache misses record one dependency edge, not lookup plus begin duplicates':
        'fn hir_query_begin' in engine and
        engine.split('fn hir_query_begin', 1)[1].split('fn hir_query_end', 1)[0].count('hir_query_record_dependency') == 0 and
        engine.split('fn hir_query_lookup', 1)[1].split('fn hir_query_begin', 1)[0].count('hir_query_record_dependency') == 1,
    'query cache validity no longer requires a global revision match':
        'query_cache_revisions, slot) == builder.query_revision' not in engine,
    'exact invalidation walks bucketed reverse dependencies': all(x in model + builder + engine + invalidation for x in [
        'hir_query_invalidate_exact', 'query_dependency_child_hashes', 'query_dependency_child_kinds',
        'query_dependency_parent_hashes', 'query_dependency_parent_kinds', 'query_dependency_buckets',
        'query_dependency_next', 'hir_query_dependency_child_same',
        'hir_query_propagate_invalidations']),
    'reverse dependency edges are indexed and exact duplicates are suppressed': all(x in engine for x in [
        'query_dependency_buckets', 'query_dependency_next', 'dependency + 1',
        'i64 existing = link - 1', 'query_dependency_child_hashes', '== child_hash',
        'query_dependency_parent_hashes', '== parent_hash', 'query_dependency_parent_c', '== parent_c']) and
        engine.count('builder.query_dependency_count += 1') == 1,
    'input-family changes batch rooted query families before one propagation': all(x in invalidation for x in [
        'hir_query_mark_kind(builder, hir_query_kind_trait_impl())',
        'hir_query_mark_kind(builder, hir_query_kind_method())',
        'hir_query_mark_kind(builder, hir_query_kind_layout())',
        'hir_query_mark_kind(builder, hir_query_kind_monomorphization())',
        'if (invalidated)', 'hir_query_propagate_invalidations(builder)']),
    'invalidation no longer fixed-point scans every dependency edge':
        'while (dependency < builder.query_dependency_count)' not in invalidation and
        'while (changed)' not in invalidation,
    'invalidation work storage is centrally owned': all(x in builder for x in [
        'query_invalidation_hashes = raz_compiler_rt_arena_create', 'query_invalidation_kinds = raz_compiler_rt_arena_create',
        'raz_compiler_rt_arena_destroy(out.query_invalidation_hashes)', 'raz_compiler_rt_arena_destroy(out.query_invalidation_kinds)']),
    'module source and exported-interface fingerprints are retained': all(x in model + fingerprints for x in [
        'query_module_source_fingerprints', 'query_module_interface_fingerprints',
        'hir_query_record_module_fingerprints', 'hir_query_module_interface_fingerprint']),
    'exported interface fingerprint is visibility-aware': all(x in fingerprints for x in [
        'declaration_visibility_values', 'visibility == 1', 'hir_module_at_offset']),
    'HIR records module fingerprints after semantic construction':
        'hir_query_record_module_fingerprints(&mut builder, &input)' in comptime,
    'incremental query modules are part of the semantic compiler graph': all(x in order for x in [
        'src/hir/query/invalidation.rz', 'src/hir/query/fingerprints.rz']),
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    for name in failed:
        print(f'query-invalidation: FAIL: {name}')
    sys.exit(1)

print('query-invalidation: PASS')
print('  semantic input families batch affected roots and walk bucketed reverse dependents only')
print('  module source/exported-interface fingerprints are retained for incremental builds')
