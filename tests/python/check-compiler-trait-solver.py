#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
from pathlib import Path


def run(args: list[str], *, cwd: Path, env: dict[str, str], should_fail: bool = False) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(args, cwd=cwd, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if should_fail:
        if result.returncode == 0:
            raise RuntimeError(f"command unexpectedly succeeded: {' '.join(args)}\n{result.stdout}\n{result.stderr}")
    elif result.returncode != 0:
        raise RuntimeError(f"command failed ({result.returncode}): {' '.join(args)}\n{result.stdout}\n{result.stderr}")
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--raz', required=True)
    parser.add_argument('--root', required=True)
    parser.add_argument('--work', required=True)
    parser.add_argument('--linker', required=True)
    ns = parser.parse_args()

    root = Path(ns.root).resolve()
    work = Path(ns.work).resolve()
    project = work / 'compiler'
    shutil.rmtree(work, ignore_errors=True)
    shutil.copytree(root / 'compiler', project)

    env = os.environ.copy()
    env['RAZ_LINKER'] = ns.linker
    run([ns.raz, 'build', str(project), '--profile', 'debug', '--force'], cwd=root, env=env)
    compiler = project / 'target' / 'debug' / ('raz-compiler.exe' if os.name == 'nt' else 'raz-compiler')
    if not compiler.is_file():
        raise RuntimeError(f'missing production compiler: {compiler}')

    # These are production-compiler semantic gates. They intentionally run only
    # through the Raz-written compiler; host compiler is compatibility-pinned bootstrap machinery.
    run([str(compiler), '--check', str(root / 'tests/traits/nested_associated_normalization.rz')], cwd=work, env=env)
    run([str(compiler), '--check', str(root / 'tests/examples/traits/trait_alias_generic_impl.rz')], cwd=work, env=env)
    run([str(compiler), '--check', str(root / 'tests/examples/traits/invalid_overlapping_generic_impl.rz')], cwd=work, env=env, should_fail=True)

    print('compiler-trait-solver: PASS')
    print('  nested associated-type normalization: production Raz compiler')
    print('  generic trait aliases/impls: production Raz compiler')
    print('  overlapping generic impls: rejected by production Raz compiler')
    print('  host compiler: not used as a feature-parity oracle')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
