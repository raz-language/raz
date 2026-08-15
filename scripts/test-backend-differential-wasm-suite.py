#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Run the maintained Forge/LLVM/WASM differential corpus."""
from __future__ import annotations
import argparse
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
CORPUS = ROOT / 'examples/backends/differential'
HARNESS = ROOT / 'scripts/test-backend-differential-wasm.py'


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--razc', required=True, type=Path)
    ap.add_argument('--forge-run', required=True, type=Path)
    ap.add_argument('--runtime', required=True, type=Path)
    ap.add_argument('--node')
    ap.add_argument('--target')
    ap.add_argument('--cpu')
    ap.add_argument('--features')
    args = ap.parse_args()

    cases = sorted(CORPUS.glob('*.rz'))
    if len(cases) < 20:
        raise SystemExit(f'backend-differential-wasm-suite: expected >=20 maintained cases, found {len(cases)}')
    failures = 0
    for case in cases:
        cmd = [sys.executable, str(HARNESS), str(case), '--razc', str(args.razc), '--forge-run', str(args.forge_run), '--runtime', str(args.runtime)]
        if args.node:
            cmd += ['--node', args.node]
        if args.target:
            cmd += ['--target', args.target]
        if args.cpu:
            cmd += ['--cpu', args.cpu]
        if args.features:
            cmd += ['--features', args.features]
        result = subprocess.run(cmd, text=True)
        if result.returncode:
            failures += 1
            print(f'backend-differential-wasm-suite: FAIL {case.name}')
        else:
            print(f'backend-differential-wasm-suite: PASS {case.name}')
    if failures:
        print(f'backend-differential-wasm-suite: {failures}/{len(cases)} failed')
        return 1
    print(f'backend-differential-wasm-suite: PASS ({len(cases)} cases across Forge/LLVM/WASM)')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
