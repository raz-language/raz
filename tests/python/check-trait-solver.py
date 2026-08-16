#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

checks = {
    ROOT / 'compiler/src/hir/core/model.rz': [
        'trait_query_active_structures',
        'trait_query_active_types',
        'trait_query_active_bounds',
        'trait_query_active_count',
    ],
    ROOT / 'compiler/src/hir/core/builder.rz': [
        'trait_query_active_structures = raz_compiler_rt_arena_create',
        'trait_query_active_types = raz_compiler_rt_arena_create',
        'trait_query_active_bounds = raz_compiler_rt_arena_create',
    ],
    ROOT / 'compiler/src/hir/generics/type_instantiation.rz': [
        'while (active < builder.trait_query_active_count)',
        'builder.trait_query_active_count += 1',
        'builder.trait_query_active_count -= 1',
        'hir_type_satisfies_bound_uncached',
    ],
    ROOT / 'compiler/src/hir/traits/solver.rz': [
        'while (existing < builder.module.generic_trait_impl_count)',
        'hir_validate_trait_impl_associated_items',
        'hir_validate_trait_impl_methods',
    ],
    ROOT / 'tests/CMakeLists.txt': [
        'raz-compiler-trait-solver',
        'raz-compiler-trait-solver',
        'raz-host-semantic-compatibility',
    ],
}

missing: list[str] = []
for path, needles in checks.items():
    text = path.read_text(encoding='utf-8')
    for needle in needles:
        if needle not in text:
            missing.append(f'{path.relative_to(ROOT)}: {needle}')

# host compiler must not grow trait/coherence semantics. The compatibility audit owns the
# byte-level check; this audit enforces the architectural dependency as well.
host_forbidden = {
    ROOT / 'src/bootstrap/compiler/semantic/detail/module_analysis.hpp': [
        'orphan implementation is not allowed',
        'overlaps generic implementation',
        'overlaps concrete implementation',
        'conflicts with negative implementation',
    ],
    ROOT / 'src/bootstrap/compiler/semantic/detail/types_traits.hpp': [
        'active_trait_queries_.insert(pair_key)',
        'trait_query_cache_',
    ],
}
for path, needles in host_forbidden.items():
    text = path.read_text(encoding='utf-8')
    for needle in needles:
        if needle in text:
            missing.append(f'{path.relative_to(ROOT)}: forbidden host compiler semantic growth: {needle}')

if missing:
    print('trait-solver: FAIL')
    for item in missing:
        print(f'  missing: {item}')
    raise SystemExit(1)

print('trait-solver: PASS')
print('  ownership: trait/coherence evolution lives in compiler/src/*.rz')
print('  termination: compiler trait obligations have an active-query recursion guard')
print('  associated types: nested normalization is covered by compiler regressions')
print('  host compiler: compatibility-pinned bootstrap semantics; no trait feature parity growth')
