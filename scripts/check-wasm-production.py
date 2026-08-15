#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Source-distribution production gate for the Raz WebAssembly backend."""
from __future__ import annotations
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
CHECKS = [
    'check-wasm-mir-coverage.py',
    'check-wasm-backend.py',
    'check-wasm-float.py',
    'check-wasm-simd.py',
    'check-wasm-runtime-memory.py',
    'check-wasm-allocator-model.py',
    'check-wasm-wasi.py',
    'check-wasm-async.py',
    'check-wasm-future.py',
    'check-wasm-optimization.py',
    'check-wasm-abi-freeze.py',
    'check-wasm-engine-validation.py',
    'check-rxe-backend.py',
    'check-rxe-malformed.py',
    'check-rxe-roundtrip.py',
    'check-rxe-freeze.py',
    'check-compiler-semantic-modules.py',
    'check-selfhost-source-set.py',
    'check-selfhost-runtime-declarations.py',
    'check-generic-layout.py',
    'check-raz-formatter-layout.py',
    'check-native-boundary.py',
    'check-repository-hygiene.py',
]


def main() -> int:
    failures: list[str] = []
    for name in CHECKS:
        path = ROOT / 'scripts' / name
        if not path.exists():
            failures.append(f'{name}: missing')
            continue
        result = subprocess.run([sys.executable, str(path)], cwd=ROOT, text=True)
        if result.returncode != 0:
            failures.append(f'{name}: exit {result.returncode}')
    corpus = sorted((ROOT / 'examples/backends/differential').glob('*.rz'))
    if len(corpus) < 20:
        failures.append(f'differential corpus: expected >=20 cases, found {len(corpus)}')
    abi = ROOT / 'docs/WASM-ABI-v1.md'
    if not abi.exists():
        failures.append('docs/WASM-ABI-v1.md: missing')
    harness = ROOT / 'scripts/test-backend-differential-wasm-suite.py'
    if not harness.exists():
        failures.append('Forge/LLVM/WASM executable differential harness: missing')
    if failures:
        print('wasm-production: FAIL')
        for failure in failures:
            print('  ' + failure)
        return 1
    print(f'wasm-production: PASS ({len(CHECKS)} source/engine gates; {len(corpus)} executable differential cases maintained)')
    print('wasm-production: executable Forge/LLVM/WASM corpus requires --razc/--forge-run/--runtime and is intentionally opt-in')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
