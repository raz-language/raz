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
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path
import os
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
    ap.add_argument('--jobs', type=int, default=min(8, os.cpu_count() or 1))
    args = ap.parse_args()

    roots = args.root or [ROOT / 'tests/examples', ROOT / 'examples']
    candidates = discover(roots)
    jobs = max(1, args.jobs)

    def qualify(index: int, case: Path) -> tuple[int, Path, int]:
        result = checked([str(args.razc), 'check', str(case)])
        return index, case, result.returncode

    qualified: dict[int, tuple[Path, int]] = {}
    if jobs == 1:
        for index, case in enumerate(candidates):
            _, resolved_case, returncode = qualify(index, case)
            qualified[index] = (resolved_case, returncode)
    else:
        with ThreadPoolExecutor(max_workers=jobs) as pool:
            futures = [pool.submit(qualify, index, case) for index, case in enumerate(candidates)]
            for future in as_completed(futures):
                index, resolved_case, returncode = future.result()
                qualified[index] = (resolved_case, returncode)

    runnable: list[Path] = []
    rejected: list[tuple[Path, int]] = []
    for index in range(len(candidates)):
        case, returncode = qualified[index]
        if returncode == 0:
            runnable.append(case)
        else:
            rejected.append((case, returncode))

    if args.list:
        for case in runnable:
            print(case.relative_to(ROOT) if case.is_relative_to(ROOT) else case)
        print(f'backend-full-corpus: {len(runnable)} runnable, {len(rejected)} check-rejected')
        return 0

    if len(runnable) < args.expected_min:
        print(f'backend-full-corpus: FAIL only {len(runnable)} runnable programs (expected >= {args.expected_min})')
        return 1

    def execute(index: int, case: Path) -> tuple[int, Path, subprocess.CompletedProcess[str]]:
        cmd = [
            sys.executable, str(HARNESS), str(case),
            '--razc', str(args.razc), '--forge-run', str(args.forge_run), '--runtime', str(args.runtime),
            '--forge-structured',
        ]
        if args.target:
            cmd += ['--target', args.target]
        if args.cpu:
            cmd += ['--cpu', args.cpu]
        if args.features:
            cmd += ['--features', args.features]
        return index, case, subprocess.run(cmd, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    results: dict[int, tuple[Path, subprocess.CompletedProcess[str]]] = {}
    if jobs == 1:
        for index, case in enumerate(runnable, 1):
            _, resolved_case, result = execute(index, case)
            results[index] = (resolved_case, result)
    else:
        with ThreadPoolExecutor(max_workers=jobs) as pool:
            futures = [pool.submit(execute, index, case) for index, case in enumerate(runnable, 1)]
            for future in as_completed(futures):
                index, resolved_case, result = future.result()
                results[index] = (resolved_case, result)

    failures = 0
    for index in range(1, len(runnable) + 1):
        case, result = results[index]
        label = case.relative_to(ROOT) if case.is_relative_to(ROOT) else case
        if result.returncode:
            failures += 1
            if result.stdout:
                print(result.stdout, end='')
            if result.stderr:
                print(result.stderr, end='')
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
