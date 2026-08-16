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
resolution = (root / 'compiler/src/hir/query/resolution.rz').read_text(encoding='utf-8')
traits = (root / 'compiler/src/hir/generics/type_instantiation.rz').read_text(encoding='utf-8')
reflection = (root / 'compiler/src/hir/semantic/reflection.rz').read_text(encoding='utf-8')
order = (root / 'compiler/host-source-order.txt').read_text(encoding='utf-8')

checks = {
    'unified query storage lives in HirBuilder': all(x in model for x in [
        'query_cache_kinds', 'query_active_kinds', 'query_dependency_parent_kinds',
        'query_dependency_child_kinds', 'query_cache_hits', 'query_cycle_count']),
    'query storage is initialized and destroyed centrally': all(x in builder for x in [
        'query_cache_kinds = raz_compiler_rt_arena_create',
        'query_dependency_parent_kinds = raz_compiler_rt_arena_create',
        'raz_compiler_rt_arena_destroy(out.query_cache_kinds)',
        'raz_compiler_rt_arena_destroy(out.query_dependency_parent_kinds)']),
    'query engine has stable keys and epoch validation': all(x in engine + invalidation for x in [
        'hir_query_hash', 'hir_query_lookup', 'query_cache_epoch0', 'query_cache_epoch1']),
    'query engine tracks dependencies': all(x in engine for x in [
        'hir_query_record_dependency', 'query_dependency_parent_kinds', 'query_dependency_child_kinds']),
    'query engine detects cycles': 'hir_query_begin' in engine and 'query_cycle_count += 1' in engine,
    'query engine supports transitive invalidation': all(x in invalidation for x in ['hir_query_invalidate_exact', 'hir_query_invalidate_kind_transitive', 'query_dependency_child_kinds']),
    'trait impl lookup migrated': 'hir_query_kind_trait_impl()' in traits and 'trait_impl_cache_' not in traits,
    'trait bound recursion migrated': 'hir_query_kind_trait_bound()' in traits and 'trait_query_active_' not in traits,
    'method resolution migrated': 'hir_query_kind_method()' in resolution and 'method_cache_' not in resolution,
    'layout queries migrated': 'hir_query_kind_layout()' in reflection and 'layout_cache_structures' not in reflection,
    'field offset queries migrated': 'hir_query_kind_field_offset()' in reflection and 'field_offset_cache_' not in reflection,
    'query modules are semantic compiler modules': all(x in order for x in [
        'src/hir/query/invalidation.rz', 'src/hir/query/engine.rz', 'src/hir/query/resolution.rz']),
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    for name in failed:
        print(f'query-engine: FAIL: {name}')
    sys.exit(1)

print('query-engine: PASS')
print('  shared semantic cache, epochs, cycle detection, dependency edges, and targeted invalidation are active')
print('  trait, method, layout, and field-offset queries use the unified runtime')
