#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Canonical Raz release qualification entry point.

Fast/source checks require no prebuilt toolchain. Runtime and full tiers require
an exact compiler/runtime/Forge tuple produced from the source revision being
qualified. A release should never be staged from binaries that have not passed
this gate.
"""
from __future__ import annotations
import argparse
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
PYTEST = ROOT / 'tests/python'


def run(label: str, cmd: list[str], *, required: bool = True) -> bool:
    print(f'\n== {label} ==')
    result = subprocess.run(cmd, cwd=ROOT, text=True)
    if result.returncode == 0:
        print(f'{label}: PASS')
        return True
    print(f'{label}: FAIL ({result.returncode})')
    if required:
        return False
    return True


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--tier', choices=['source', 'runtime', 'full'], default='source')
    ap.add_argument('--razc', type=Path)
    ap.add_argument('--forge-run', type=Path)
    ap.add_argument('--runtime', type=Path)
    ap.add_argument('--node', default=shutil.which('node'))
    ap.add_argument('--skip-wasm', action='store_true')
    ap.add_argument('--skip-full-corpus', action='store_true')
    ap.add_argument('--expected-corpus-min', type=int, default=100)
    args = ap.parse_args()

    ok = True
    ok &= run('source qualification', [sys.executable, str(PYTEST / 'check-source.py')])
    ok &= run('RXE semantic/reference qualification', [sys.executable, str(PYTEST / 'test-backend-differential-rxe-suite.py')])
    if args.tier == 'source':
        return 0 if ok else 1

    missing = [name for name, value in [('razc', args.razc), ('forge-run', args.forge_run), ('runtime', args.runtime)] if not value]
    if missing:
        print('release-gate: runtime/full tier requires --' + ', --'.join(missing))
        return 2

    native = [
        sys.executable, str(PYTEST / 'test-backend-differential-suite.py'),
        '--razc', str(args.razc), '--forge-run', str(args.forge_run), '--runtime', str(args.runtime),
    ]
    ok &= run('Forge↔LLVM maintained executable parity', native)

    if not args.skip_wasm:
        if not args.node:
            print('release-gate: Node.js is required for WASM runtime qualification (or use --skip-wasm for development only)')
            return 2
        wasm = [
            sys.executable, str(PYTEST / 'test-backend-differential-wasm-suite.py'),
            '--razc', str(args.razc), '--forge-run', str(args.forge_run), '--runtime', str(args.runtime), '--node', str(args.node),
        ]
        ok &= run('Forge↔LLVM↔WASM maintained executable parity', wasm)

    ok &= run('stdlib executable qualification', [sys.executable, str(PYTEST / 'test-stdlib-execution.py'), '--razc', str(args.razc), '--forge-codegen', str(args.forge_run), '--runtime', str(args.runtime)])
    if args.tier == 'runtime':
        return 0 if ok else 1

    if not args.skip_full_corpus:
        full = [
            sys.executable, str(PYTEST / 'test-backend-full-corpus.py'),
            '--razc', str(args.razc), '--forge-run', str(args.forge_run), '--runtime', str(args.runtime),
            '--expected-min', str(args.expected_corpus_min),
        ]
        ok &= run('full discovered Forge↔LLVM executable corpus', full)

    # Tooling checks that depend on a production compiler are intentionally
    # separate from check-source's structural contracts.
    for label, script in [
        ('production LSP', 'check-production-lsp.py'),
        ('bindgen round trip', 'check-bindgen.py'),
        ('C-header round trip', 'check-c-header.py'),
    ]:
        cmd = [sys.executable, str(PYTEST / script), '--raz', str(args.razc)]
        ok &= run(label, cmd)

    print('\nrelease-gate: ' + ('PASS' if ok else 'FAIL'))
    return 0 if ok else 1


if __name__ == '__main__':
    raise SystemExit(main())
