#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""End-to-end native artifact layout qualification.

Proves that a real executable with a local package dependency is built from
canonical package/module objects, links exactly once, and never retains the
legacy whole-project object layout.
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def run(command: list[str], *, cwd: Path, env: dict[str, str]) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(command, cwd=cwd, env=env, text=True, capture_output=True)
    if result.returncode != 0:
        raise RuntimeError(
            f"command failed ({result.returncode}): {' '.join(command)}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    return result


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--raz', required=True, type=Path)
    ns = ap.parse_args()
    compiler = ns.raz.resolve()
    if not compiler.is_file():
        raise SystemExit(f'compiler not found: {compiler}')

    suffix = '.exe' if os.name == 'nt' else ''
    obj_suffix = '.obj' if os.name == 'nt' else '.o'

    with tempfile.TemporaryDirectory(prefix='raz-native-layout-') as raw:
        root = Path(raw)
        core = root / 'core'
        app = root / 'app'
        (core / 'src').mkdir(parents=True)
        (app / 'src').mkdir(parents=True)
        (core / 'raz.toml').write_text(
            '[package]\nname = "core"\nversion = "1.0.0"\nkind = "library"\n'
            'entry = "src/lib.rz"\n\n[dependencies]\n', encoding='utf-8', newline='\n')
        (core / 'src/lib.rz').write_text(
            'namespace core;\npublic fn value() -> i64 { return 42; }\n', encoding='utf-8', newline='\n')
        (app / 'raz.toml').write_text(
            '[package]\nname = "app"\nversion = "1.0.0"\nkind = "executable"\n'
            'entry = "src/main.rz"\n\n[dependencies]\ncore = "../core"\n', encoding='utf-8', newline='\n')
        (app / 'src/helper.rz').write_text(
            'import core;\nfn answer() -> i64 { return core::value(); }\n', encoding='utf-8', newline='\n')
        (app / 'src/main.rz').write_text(
            'fn main() -> i64 { return answer() - 42; }\n', encoding='utf-8', newline='\n')

        env = dict(os.environ)
        env['RAZ_HOME'] = str(root / 'raz-home')

        for profile, extra in (('debug', []), ('release', ['--release'])):
            result = run([str(compiler), 'build', *extra], cwd=app, env=env)
            if result.stdout.count('Linking app v1.0.0') != 1:
                raise RuntimeError(f'{profile}: expected exactly one package-unit link\n{result.stdout}')

            profile_root = app / 'target' / profile
            binary = profile_root / 'bin' / f'app{suffix}'
            package_root = profile_root / 'packages'
            if not binary.is_file():
                raise RuntimeError(f'{profile}: missing executable: {binary}')
            if (profile_root / f'app{suffix}').exists():
                raise RuntimeError(f'{profile}: legacy flat executable survived')
            legacy_obj = profile_root / 'obj' / f'app{obj_suffix}'
            if legacy_obj.exists():
                raise RuntimeError(f'{profile}: legacy whole-project object survived: {legacy_obj}')
            if (profile_root / 'obj').exists():
                raise RuntimeError(f'{profile}: legacy obj/ category was materialized: {profile_root / "obj"}')

            dependency_objects = sorted(package_root.glob(f'*{obj_suffix}'))
            root_module_objects = sorted(package_root.glob(f'*/*{obj_suffix}'))
            if len(dependency_objects) != 1:
                raise RuntimeError(f'{profile}: expected one dependency package object, got {dependency_objects}')
            if len(root_module_objects) < 2:
                raise RuntimeError(f'{profile}: expected separate root module objects, got {root_module_objects}')
            if any(p.stat().st_size == 0 for p in dependency_objects + root_module_objects):
                raise RuntimeError(f'{profile}: zero-byte native object emitted')

            executed = subprocess.run([str(binary)], cwd=app, env=env)
            if executed.returncode != 0:
                raise RuntimeError(f'{profile}: executable returned {executed.returncode}, expected 0')

            unexpected_categories = []
            for category in ('lib', 'ir', 'modules'):
                path = profile_root / category
                if path.exists() and not any(path.iterdir()):
                    unexpected_categories.append(path)
            if unexpected_categories:
                raise RuntimeError(f'{profile}: empty target categories were materialized: {unexpected_categories}')

    print('native-artifact-layout: PASS (dep package + root module objects + single link + bin + debug/release)')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
