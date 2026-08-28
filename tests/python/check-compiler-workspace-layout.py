#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
import sys
ROOT = Path(__file__).resolve().parents[2]
C = ROOT / 'compiler'
SRC = C / 'src'
expected = {
    'raz_lexer': 'raz-lexer',
    'raz_parser': 'raz-parser',
    'raz_query': 'raz-query',
    'raz_hir': 'raz-hir',
    'raz_mir': 'raz-mir',
    'raz_mir_opt': 'raz-mir-opt',
    'raz_borrowck': 'raz-borrowck',
    'raz_codegen_forge': 'raz-codegen-forge',
    'raz_codegen_llvm': 'raz-codegen-llvm',
    'raz_codegen_wasm': 'raz-codegen-wasm',
    'raz_codegen_rxe': 'raz-codegen-rxe',
    'raz_codegen_web': 'raz-codegen-web',
    'raz_driver': 'raz-driver',
}
failed=[]

for sibling in C.glob('raz_*'):
    if sibling.is_dir(): failed.append(f'compiler package must live under compiler/src: {sibling.name}')
for folder, package in expected.items():
    manifest=SRC/folder/'raz.toml'
    lib=SRC/folder/'src/lib.rz'
    if not manifest.is_file() or not lib.is_file(): failed.append(f'missing {folder} package')
    elif f'name = "{package}"' not in manifest.read_text(encoding='utf-8'): failed.append(f'wrong package name: {folder}')
for legacy in ('frontend','middle','backend','driver'):
    if (C/'src'/legacy).exists(): failed.append(f'legacy compiler/src/{legacy} returned')
root=(C/'raz.toml').read_text(encoding='utf-8')
if 'driver = "src/raz_driver"' not in root: failed.append('root compiler does not depend on raz_driver')
if (C/'src/main.rz').read_text(encoding='utf-8').count('compiler_driver_main') != 1: failed.append('main.rz is not a tiny driver entry')
# HIR must not depend on MIR; this was an accidental coupling exposed by the split.
for p in (SRC/'raz_hir/src').rglob('*.rz'):
    if 'raz_mir::' in p.read_text(encoding='utf-8') or 'import mir::' in p.read_text(encoding='utf-8'):
        failed.append(f'HIR -> MIR dependency: {p.relative_to(ROOT)}')
        break
driver=(SRC/'raz_driver/raz.toml').read_text(encoding='utf-8')
for dep in ('raz_codegen_forge','raz_codegen_llvm','raz_codegen_wasm','raz_codegen_rxe','raz_codegen_web'):
    if dep not in driver: failed.append(f'driver missing backend package dependency: {dep}')
if (SRC/'raz_codegen').exists(): failed.append('legacy monolithic raz_codegen package returned')

# Borrow checking is a real compiler phase above MIR. MIR owns the facts in its
# IR but must never depend on the legality-analysis package.
borrow_manifest=(SRC/'raz_borrowck/raz.toml').read_text(encoding='utf-8')
if 'mir = "../raz_mir"' not in borrow_manifest: failed.append('raz_borrowck missing raz_mir dependency')
mir_manifest=(SRC/'raz_mir/raz.toml').read_text(encoding='utf-8')
if 'raz_borrowck' in mir_manifest or 'borrowck' in mir_manifest: failed.append('raz_mir must not depend on raz_borrowck')
if 'borrowck = "../raz_borrowck"' not in driver: failed.append('driver missing raz_borrowck dependency')
for p in (SRC/'raz_mir/src').rglob('*.rz'):
    if 'raz_borrowck::' in p.read_text(encoding='utf-8') or 'import borrowck::' in p.read_text(encoding='utf-8'):
        failed.append(f'MIR -> borrowck dependency: {p.relative_to(ROOT)}')
        break


# MIR optimization is a policy package above core MIR. It may consume MIR, but
# core MIR must not depend back on optimization transforms.
mir_opt_manifest=(SRC/'raz_mir_opt/raz.toml').read_text(encoding='utf-8')
if 'mir = "../raz_mir"' not in mir_opt_manifest: failed.append('raz_mir_opt missing raz_mir dependency')
if 'mir_opt = "../raz_mir_opt"' not in driver: failed.append('driver missing raz_mir_opt dependency')
for p in (SRC/'raz_mir/src').rglob('*.rz'):
    text=p.read_text(encoding='utf-8')
    if 'raz_mir_opt::' in text or 'import mir_opt::' in text or '::transform::' in text:
        failed.append(f'MIR -> mir-opt dependency/transform leakage: {p.relative_to(ROOT)}')
        break
if (SRC/'raz_mir/src/mir/transform').exists(): failed.append('optimization transforms leaked back into raz_mir')

# Query state is a real package boundary: HIR owns semantic operations but the
# shared query database/context must not drift back into raz_hir.
hir_manifest=(SRC/'raz_hir/raz.toml').read_text(encoding='utf-8')
if 'querydb = "../raz_query"' not in hir_manifest: failed.append('raz_hir missing raz_query dependency')
if (SRC/'raz_hir/src/hir/query/context.rz').exists(): failed.append('query context drifted back into raz_hir')
query_context=SRC/'raz_query/src/query/context.rz'
if not query_context.is_file() or 'public struct HirQueryContext' not in query_context.read_text(encoding='utf-8'):
    failed.append('raz_query does not own HirQueryContext')

if failed:
    print('compiler-workspace-layout: FAIL')
    for x in failed: print('  '+x)
    sys.exit(1)
print('compiler-workspace-layout: PASS (focused compiler packages + split codegen backends)')
