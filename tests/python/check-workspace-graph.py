#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations
import argparse
from pathlib import Path
import shutil
import subprocess


def run(args: list[str]) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(args, text=True, capture_output=True)
    if result.returncode != 0:
        raise SystemExit(f"command failed: {' '.join(args)}\n{result.stdout}\n{result.stderr}")
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--raz', required=True)
    parser.add_argument('--fixture', required=True)
    parser.add_argument('--work', required=True)
    args = parser.parse_args()

    work = Path(args.work)
    if work.exists():
        shutil.rmtree(work)
    shutil.copytree(args.fixture, work)
    shutil.rmtree(work / 'target', ignore_errors=True)
    shutil.rmtree(work / 'target', ignore_errors=True)

    run([args.raz, 'build', str(work), '--force', '--color', 'never'])
    state = work / 'target' / 'cache' / 'workspace-v1.state'
    if not state.is_file():
        raise SystemExit('workspace graph was not persisted')
    text = state.read_text()
    required = [
        'raz-workspace-v1',
        'module "hello-package@0.1.0::main"',
        'module "hello-package@0.1.0::util::math"',
        'edge "hello-package@0.1.0::main" "hello-package@0.1.0::util::math"',
    ]
    for item in required:
        if item not in text:
            raise SystemExit(f'missing workspace state item: {item}')

    util = work / 'src' / 'util' / 'math.rz'
    util.write_text(util.read_text() + '\nfn internal_increment(i64 value) -> i64 { return value + 1; }\n')
    changed = run([args.raz, 'build', str(work), '--verbose', '--color', 'never'])
    combined = changed.stdout + changed.stderr
    if 'Workspace 2 dirty module(s)' not in combined and 'Workspace 2 dirty module(s),' not in combined:
        raise SystemExit(f'dependent dirty propagation was not reported:\n{combined}')
    if '1 compiled, 1 fresh' not in combined:
        raise SystemExit(f'baseline incremental behavior changed unexpectedly:\n{combined}')

    print('workspace-graph: PASS (persistent graph, fingerprints, dependency edge, transitive dirty propagation)')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
