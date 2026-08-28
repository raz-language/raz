#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import sys

root = Path(__file__).resolve().parents[2]
model = (root / 'compiler/src/raz_hir/src/hir/core/model.rz').read_text(encoding='utf-8')
query_context = (root / 'compiler/src/raz_query/src/query/context.rz').read_text(encoding='utf-8')
model += '\n' + query_context
builder = (root / 'compiler/src/raz_hir/src/hir/core/builder.rz').read_text(encoding='utf-8')
identity = (root / 'compiler/src/raz_hir/src/hir/query/identity.rz').read_text(encoding='utf-8')
instantiate = (root / 'compiler/src/raz_hir/src/hir/generics/instantiate.rz').read_text(encoding='utf-8')
types = (root / 'compiler/src/raz_hir/src/hir/generics/type_instantiation.rz').read_text(encoding='utf-8')
order = {path.relative_to(root / 'compiler').as_posix() for path in list((root / 'compiler').rglob('*.rz'))}
combined = instantiate + '\n' + types

legacy = [
    'generic_function_cache_templates', 'generic_function_cache_argument_counts',
    'generic_function_cache_hashes', 'generic_function_cache_results',
    'generic_struct_cache_templates', 'generic_struct_cache_argument_counts',
    'generic_struct_cache_hashes', 'generic_struct_cache_results',
    'generic_enum_cache_templates', 'generic_enum_cache_argument_counts',
    'generic_enum_cache_hashes', 'generic_enum_cache_results',
    'associated_type_cache_structures', 'associated_type_cache_traits',
    'associated_type_cache_item_offsets', 'associated_type_cache_item_lengths',
    'associated_type_cache_result_structures', 'associated_type_cache_result_types',
]
checks = {
    'legacy generic and associated-type result caches removed': not any(x in model + builder + combined for x in legacy),
    'canonical monomorph identities are stored in HirQueryContext': all(x in model for x in [
        'query_monomorph_count', 'query_monomorph_entity_kinds', 'query_monomorph_argument_starts',
        'query_monomorph_argument_structures', 'query_monomorph_argument_types',
        'query_monomorph_buckets', 'query_monomorph_next']),
    'canonical identity storage is centrally managed': all(x in builder for x in [
        'queries.query_monomorph_entity_kinds = raz_compiler_rt_arena_create',
        'queries.query_monomorph_argument_structures = raz_compiler_rt_arena_create',
        'raz_compiler_rt_arena_destroy(out.queries.query_monomorph_entity_kinds)',
        'raz_compiler_rt_arena_destroy(out.queries.query_monomorph_argument_structures)']),
    'monomorph identities compare exact packed arguments': all(x in identity for x in [
        'hir_query_intern_monomorphization', 'hir_query_monomorph_arguments_same',
        'query_monomorph_argument_structures', 'query_monomorph_argument_types',
        'query_monomorph_buckets', 'query_monomorph_next']),
    'generic function instantiation is query orchestrated': all(x in instantiate for x in [
        'hir_query_intern_monomorphization', 'hir_query_kind_monomorphization()',
        'hir_query_begin(builder, hir_query_kind_monomorphization()',
        'hir_instantiate_generic_function_uncached']),
    'generic aggregate instantiation is query orchestrated': all(x in types for x in [
        'hir_instantiate_generic_struct_uncached', 'hir_instantiate_generic_enum_uncached',
        'hir_query_intern_monomorphization', 'hir_query_kind_monomorphization()']),
    'associated type normalization uses canonical symbol/type identity': all(x in instantiate for x in [
        'hir_query_kind_associated_type()', 'hir_query_intern_symbol',
        'hir_query_intern_value_type', 'item_symbol', 'base_type']),
    'query identity is a semantic compiler module': 'src/raz_hir/src/hir/query/identity.rz' in order,
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    for name in failed:
        print(f'query-generics: FAIL: {name}')
    sys.exit(1)

print('query-generics: PASS')
print('  monomorphization uses collision-safe canonical request IDs and shared query orchestration')
print('  associated-type normalization uses canonical SymbolId/TypeId identities')
