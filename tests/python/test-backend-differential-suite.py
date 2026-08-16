#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Run the maintained Forge-vs-LLVM differential corpus."""
from __future__ import annotations
import argparse
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
CORPUS = ROOT / 'tests' / 'examples' / 'backends' / 'differential'
HARNESS = ROOT / 'tests' / 'python' / 'test-backend-differential.py'


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--razc', required=True, type=Path)
    ap.add_argument('--forge-run', required=True, type=Path)
    ap.add_argument('--runtime', type=Path)
    ap.add_argument('--target')
    ap.add_argument('--cpu')
    ap.add_argument('--features')
    args = ap.parse_args()

    cases = sorted(CORPUS.glob('*.rz'))
    if not cases:
        raise SystemExit('backend-differential-suite: no corpus cases found')
    failures = 0
    for case in cases:
        cmd = [sys.executable, str(HARNESS), str(case), '--razc', str(args.razc), '--forge-run', str(args.forge_run)]
        if args.runtime:
            cmd += ['--runtime', str(args.runtime)]
        if args.target:
            cmd += ['--target', args.target]
        if args.cpu:
            cmd += ['--cpu', args.cpu]
        if args.features:
            cmd += ['--features', args.features]
        result = subprocess.run(cmd, text=True)
        if result.returncode != 0:
            failures += 1
            print(f'backend-differential-suite: FAIL {case.name}')
        else:
            print(f'backend-differential-suite: PASS {case.name}')
    if failures:
        print(f'backend-differential-suite: {failures}/{len(cases)} failed')
        return 1
    print(f'backend-differential-suite: PASS ({len(cases)} cases)')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
