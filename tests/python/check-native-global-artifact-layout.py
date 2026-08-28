#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Qualify cross-object native globals in the canonical packages/** layout.

Covers a scalar global owned by a dependency package and an aggregate global
owned by a non-entry root module.  The final executable must link exactly once
from package/module objects with no whole-project obj/ fallback.
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
    args = ap.parse_args()
    raz = args.raz.resolve()
    suffix = '.exe' if os.name == 'nt' else ''
    obj_suffix = '.obj' if os.name == 'nt' else '.o'

    with tempfile.TemporaryDirectory(prefix='raz-global-artifacts-') as td:
        root = Path(td)
        core = root / 'core'
        app = root / 'app'
        (core / 'src').mkdir(parents=True)
        (app / 'src').mkdir(parents=True)
        (core / 'raz.toml').write_text(
            '[package]\nname = "core"\nversion = "1.0.0"\nkind = "library"\n'
            'entry = "src/lib.rz"\n\n[dependencies]\n', encoding='utf-8', newline='\n')
        (core / 'src/lib.rz').write_text(
            'namespace core;\n'
            'global mut i64 base = 40;\n'
            'public fn value() -> i64 { return base; }\n', encoding='utf-8', newline='\n')
        (app / 'raz.toml').write_text(
            '[package]\nname = "global-layout"\nversion = "0.1.0"\nkind = "executable"\n'
            'entry = "src/main.rz"\n\n[dependencies]\ncore = "../core"\n', encoding='utf-8', newline='\n')
        (app / 'src/helper.rz').write_text(
            'struct Pair { i64 left; i64 right; }\n'
            'global Pair pair = Pair { left: 1, right: 1 };\n'
            'fn extra() -> i64 { return pair.left + pair.right; }\n', encoding='utf-8', newline='\n')
        (app / 'src/main.rz').write_text(
            'import core;\n'
            'fn main() -> i64 { return core::value() + extra() - 42; }\n', encoding='utf-8', newline='\n')

        env = dict(os.environ)
        env['RAZ_HOME'] = str(root / 'raz-home')
        for profile, extra in (('debug', []), ('release', ['--release'])):
            result = run([str(raz), 'build', *extra], cwd=app, env=env)
            if result.stdout.count('Linking global-layout v0.1.0') != 1:
                raise RuntimeError(f'{profile}: expected exactly one modular link\n{result.stdout}')
            profile_root = app / 'target' / profile
            exe = profile_root / 'bin' / f'global-layout{suffix}'
            package_root = profile_root / 'packages'
            if not exe.is_file():
                raise RuntimeError(f'{profile}: executable missing from target/{profile}/bin')
            dependency_objects = sorted(package_root.glob(f'*{obj_suffix}'))
            root_module_objects = sorted(package_root.glob(f'*/*{obj_suffix}'))
            if len(dependency_objects) != 1:
                raise RuntimeError(f'{profile}: expected dependency global owner object, got {dependency_objects}')
            if len(root_module_objects) < 2:
                raise RuntimeError(f'{profile}: expected separate root module objects, got {root_module_objects}')
            if any(p.stat().st_size == 0 for p in dependency_objects + root_module_objects):
                raise RuntimeError(f'{profile}: zero-byte package/module object emitted')
            objdir = profile_root / 'obj'
            if objdir.exists() and any(objdir.iterdir()):
                raise RuntimeError(f'{profile}: whole-project linker scratch survived: {list(objdir.iterdir())}')
            executed = subprocess.run([str(exe)], cwd=app, env=env)
            if executed.returncode != 0:
                raise RuntimeError(f'{profile}: executable returned {executed.returncode}, expected 0')

    print('native-global-artifact-layout: PASS (scalar + aggregate globals owned by package/module objects)')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
