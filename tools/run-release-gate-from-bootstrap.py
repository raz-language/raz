#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Run the canonical release gate using artifacts from tools/bootstrap.py."""
from __future__ import annotations
import argparse
from pathlib import Path
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
EXE = '.exe' if os.name == 'nt' else ''


def one(root: Path, names: tuple[str, ...]) -> Path:
    matches: list[Path] = []
    for name in names:
        matches.extend(root.rglob(name))
    files = sorted({p.resolve() for p in matches if p.is_file()}, key=lambda p: (len(p.parts), str(p)))
    if not files:
        raise SystemExit(f'release artifacts: unable to find any of {names} below {root}')
    return files[0]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--tier', choices=['runtime', 'full'], default='full')
    ap.add_argument('--host-preset', choices=['debug', 'release'], default='release')
    ap.add_argument('--bootstrap-profile', choices=['debug', 'release'], default='release')
    ap.add_argument('--skip-wasm', action='store_true')
    ap.add_argument('--skip-full-corpus', action='store_true')
    ap.add_argument('--expected-corpus-min', type=int, default=100)
    args = ap.parse_args()

    qualification = ROOT / 'target/bootstrap'
    compiler = qualification / 'repro-1' / 'target' / args.bootstrap_profile / f'raz-compiler{EXE}'
    if not compiler.is_file():
        raise SystemExit(f'release artifacts: self-hosted compiler not found: {compiler}')

    host = ROOT / 'build' / args.host_preset
    forge_run = one(host, (f'forge-codegen{EXE}',))
    runtime = one(host, ('raz_runtime.lib', 'libraz_runtime.a'))

    cmd = [
        sys.executable, str(ROOT / 'tests/python/release-gate.py'),
        '--tier', args.tier,
        '--razc', str(compiler),
        '--forge-run', str(forge_run),
        '--runtime', str(runtime),
        '--expected-corpus-min', str(args.expected_corpus_min),
    ]
    if args.skip_wasm: cmd.append('--skip-wasm')
    if args.skip_full_corpus: cmd.append('--skip-full-corpus')
    print('release artifacts:')
    print(f'  compiler : {compiler}')
    print(f'  forge    : {forge_run}')
    print(f'  runtime  : {runtime}')
    return subprocess.run(cmd, cwd=ROOT).returncode


if __name__ == '__main__':
    raise SystemExit(main())
