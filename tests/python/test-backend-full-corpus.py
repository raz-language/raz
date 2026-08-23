#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Discover runnable Raz corpus programs and enforce Forge/LLVM parity.

A source participates when it contains `fn main` and `raz check <source>`
succeeds with the supplied production compiler. The selected programs are then
executed through the normal differential harness and must agree on exit status,
stdout, and stderr.
"""
from __future__ import annotations
import argparse
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
HARNESS = ROOT / 'tests/python/test-backend-differential.py'
MAIN_RE = re.compile(r'\bfn\s+main\s*\(')


def checked(cmd: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def discover(roots: list[Path]) -> list[Path]:
    out: list[Path] = []
    seen: set[Path] = set()
    for root in roots:
        if not root.exists():
            continue
        for path in sorted(root.rglob('*.rz')):
            resolved = path.resolve()
            if resolved in seen:
                continue
            seen.add(resolved)
            try:
                text = path.read_text(errors='replace')
            except OSError:
                continue
            if MAIN_RE.search(text):
                out.append(path)
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--razc', required=True, type=Path)
    ap.add_argument('--forge-run', required=True, type=Path)
    ap.add_argument('--runtime', required=True, type=Path)
    ap.add_argument('--root', action='append', type=Path)
    ap.add_argument('--target')
    ap.add_argument('--cpu')
    ap.add_argument('--features')
    ap.add_argument('--expected-min', type=int, default=100)
    ap.add_argument('--list', action='store_true')
    args = ap.parse_args()

    roots = args.root or [ROOT / 'tests/examples', ROOT / 'examples']
    candidates = discover(roots)
    runnable: list[Path] = []
    rejected: list[tuple[Path, int]] = []
    for case in candidates:
        result = checked([str(args.razc), 'check', str(case)])
        if result.returncode == 0:
            runnable.append(case)
        else:
            rejected.append((case, result.returncode))

    if args.list:
        for case in runnable:
            print(case.relative_to(ROOT) if case.is_relative_to(ROOT) else case)
        print(f'backend-full-corpus: {len(runnable)} runnable, {len(rejected)} check-rejected')
        return 0

    if len(runnable) < args.expected_min:
        print(f'backend-full-corpus: FAIL only {len(runnable)} runnable programs (expected >= {args.expected_min})')
        return 1

    failures = 0
    for index, case in enumerate(runnable, 1):
        cmd = [
            sys.executable, str(HARNESS), str(case),
            '--razc', str(args.razc), '--forge-run', str(args.forge_run), '--runtime', str(args.runtime),
        ]
        if args.target: cmd += ['--target', args.target]
        if args.cpu: cmd += ['--cpu', args.cpu]
        if args.features: cmd += ['--features', args.features]
        result = subprocess.run(cmd, text=True)
        label = case.relative_to(ROOT) if case.is_relative_to(ROOT) else case
        if result.returncode:
            failures += 1
            print(f'backend-full-corpus: FAIL [{index}/{len(runnable)}] {label}')
        else:
            print(f'backend-full-corpus: PASS [{index}/{len(runnable)}] {label}')

    if failures:
        print(f'backend-full-corpus: FAIL {failures}/{len(runnable)} behavioral mismatches')
        return 1
    print(f'backend-full-corpus: PASS ({len(runnable)} runnable programs, exit/stdout/stderr parity)')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
