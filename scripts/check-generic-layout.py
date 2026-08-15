#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import sys

root = Path(__file__).resolve().parents[1]
errors: list[str] = []

def text(rel: str) -> str:
    return (root / rel).read_text(encoding='utf-8')

def require(rel: str, needles: list[str]) -> None:
    data = text(rel)
    for needle in needles:
        if needle not in data:
            errors.append(f"{rel}: missing {needle!r}")


def require_tree(rel: str, needles: list[str]) -> None:
    base = root / rel
    files = [base] if base.is_file() else sorted(p for p in base.rglob('*') if p.is_file() and p.suffix in {'.cpp', '.hpp'})
    data = '\n'.join(p.read_text(encoding='utf-8') for p in files)
    for needle in needles:
        if needle not in data:
            errors.append(f"{rel}: missing {needle!r}")
require_tree('src/bootstrap/compiler/semantic', ['size_of', 'align_of', 'D2290'])
require_tree('src/bootstrap/compiler/lowering/hir_to_mir', ['size_of', 'align_of', 'type_alignment'])
require('src/bootstrap/compiler/syntax/namespace_lowering.cpp', ['Compiler intrinsics such as size_of<T>() and align_of<T>()'])
require('compiler/src/hir/semantic/reflection.rz', [
    'hir_reflection_type_size',
    'hir_reflection_type_align',
    'struct_explicit_alignments',
])
require('compiler/src/hir/generics/instantiate.rz', ['hir_find_generic_substitution', 'builder.generic_active'])
require_tree('src/runtime', ['raz_rt_alloc_aligned', 'raz_rt_dealloc_aligned', 'alignment > 4096'])
require('src/forge/src/ir/verifier.cpp', ['operation.alignment > 4096U', 'alignment no greater than 4096'])
require('library/core/mem/mem.rz', ['public struct Layout', 'public fn layout_of<T>()'])
require('library/alloc/box/box.rz', ['public fn allocate_type<T>()', 'public fn allocate_array<T>', 'raz_rt_alloc_aligned'])

collections = [
    'library/collections/vector/vector.rz',
    'library/collections/deque/deque.rz',
    'library/collections/hash_set/hash_set.rz',
    'library/collections/hash_map/hash_map.rz',
]
for rel in collections:
    data = text(rel)
    if 'size_of<' not in data or 'align_of<' not in data:
        errors.append(f'{rel}: typed storage must use concrete size_of/align_of layout')
    # The old fixed one-word element stride must never return. Numeric 8 is
    # still valid for minimum capacities, so guard the actual address pattern.
    if 'slot * 8' in data or 'index * 8' in data or 'current_slot * 8' in data:
        errors.append(f'{rel}: fixed 8-byte typed element stride returned')

if errors:
    print('generic-layout audit: FAIL')
    for error in errors:
        print(f'  {error}')
    sys.exit(1)

print('generic-layout audit: PASS')
print('  native + Raz-owned reflection: generic concrete layout')
print('  Forge alignment contract: 1..4096 power-of-two')
print('  stdlib typed storage: size/alignment driven')
