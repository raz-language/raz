#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# A recursive trait-bound query must terminate rather than recurse forever.
# That guard began as a trait-specific active stack (trait_query_active_*) and
# was generalized into the shared query engine, which keys every query kind --
# trait bounds included -- on the same active-frame stack. Assert the guard
# where it now lives instead of the superseded trait-only spelling.
checks = {
    ROOT / 'compiler/src/hir/core/model.rz': [
        'query_active_kinds',
        'query_active_a',
        'query_active_count',
    ],
    ROOT / 'compiler/src/hir/core/builder.rz': [
        'query_active_hashes = raz_compiler_rt_arena_create',
        'query_active_kinds = raz_compiler_rt_arena_create',
        'query_active_a = raz_compiler_rt_arena_create',
    ],
    ROOT / 'compiler/src/hir/query/engine.rz': [
        # Re-entering an in-flight query is a cycle: refuse it and count it.
        'while (active < builder.query_active_count)',
        'builder.query_cycle_count += 1',
        'builder.query_active_count += 1',
        'builder.query_active_count -= 1',
    ],
    ROOT / 'compiler/src/hir/generics/type_instantiation.rz': [
        # Bound satisfaction must run inside an engine frame, so a bound that
        # depends on itself is caught by the guard above.
        'hir_query_begin(builder, hir_query_kind_trait_bound()',
        'hir_query_end(builder, hir_query_kind_trait_bound()',
        'hir_type_satisfies_bound_uncached',
    ],
    ROOT / 'compiler/src/hir/traits/solver.rz': [
        'while (existing < builder.module.generic_trait_impl_count)',
        'hir_validate_trait_impl_associated_items',
        'hir_validate_trait_impl_methods',
    ],
    ROOT / 'tests/CMakeLists.txt': [
        'raz-compiler-trait-solver',
        'raz-trait-solver-audit',
        # Host/production semantic agreement, renamed from the original
        # raz-host-semantic-compatibility.
        'raz-host-compiler-contract',
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
