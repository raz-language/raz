#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Executable qualification for reachability-pruned reactive JS runtime helpers."""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def run(cmd: list[str], cwd: Path, env: dict[str, str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, cwd=cwd, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)


def build_js(raz: Path, work: Path, fixture_name: str, env: dict[str, str]) -> str:
    fixture = ROOT / 'tests' / 'examples' / 'web' / fixture_name
    project = work / fixture_name
    shutil.copytree(fixture, project)
    built = run([str(raz), 'build', '--release', 'raz.toml'], project, env)
    if built.returncode != 0:
        raise RuntimeError(f'{fixture_name} build failed:\n{built.stdout}')
    candidates = sorted((project / 'dist' / 'assets').glob('app.*.js'))
    if len(candidates) != 1:
        raise RuntimeError(f'{fixture_name}: expected one fingerprinted app JS, found {len(candidates)}')
    return candidates[0].read_text(encoding='utf-8')


def require(js: str, fixture: str, present: tuple[str, ...], absent: tuple[str, ...]) -> None:
    missing = [token for token in present if token not in js]
    leaked = [token for token in absent if token in js]
    if missing or leaked:
        if missing:
            print(f'web-runtime-pruning-runtime: {fixture}: missing helpers {missing}')
        if leaked:
            print(f'web-runtime-pruning-runtime: {fixture}: retained unreachable helpers {leaked}')
        raise SystemExit(1)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--raz', required=True)
    ap.add_argument('--work-root', required=True)
    args = ap.parse_args()

    raz = Path(args.raz).resolve()
    work = Path(args.work_root).resolve()
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)

    env = os.environ.copy()
    env['RAZ_HOME'] = str(ROOT)
    runtime_library = ROOT / 'build' / 'release' / 'src' / 'runtime' / 'libraz_runtime.a'
    if runtime_library.is_file():
        env['RAZ_RUNTIME_LIBRARY'] = str(runtime_library)

    try:
        plain = build_js(raz, work, 'reactive', env)
        require(
            plain,
            'reactive',
            ("root.innerHTML = exportedText('raz_web_html')", "exportedText('raz_web_js')"),
            ('const patchChildren =', 'const dispatchNode =', 'const syncRouteChange =', 'const pumpHttp =', 'const applyStateBindings =', 'const renderOwnedScope =', 'const encoder = new TextEncoder()', 'const razWeb = {', 'const imports = { raz_web: razWeb }'),
        )

        state = build_js(raz, work, 'component-state', env)
        require(
            state,
            'component-state',
            ('const patchChildren =', 'const dispatchNode =', 'const applyStateBindings =', 'const renderOwnedScope ='),
            ('const syncRouteChange =', 'const pumpHttp ='),
        )

        routing = build_js(raz, work, 'routing-reactive', env)
        require(
            routing,
            'routing-reactive',
            ('const patchChildren =', 'const syncRouteChange =', "addEventListener('popstate'"),
            ('const dispatchNode =', 'const pumpHttp =', 'const applyStateBindings =', 'const renderOwnedScope ='),
        )

        http = build_js(raz, work, 'fetch-stdlib', env)
        require(
            http,
            'fetch-stdlib',
            ('const patchChildren =', 'const pumpHttp = async () =>'),
            ('const dispatchNode =', 'const syncRouteChange =', 'const applyStateBindings =', 'const renderOwnedScope ='),
        )
    except RuntimeError as exc:
        print(f'web-runtime-pruning-runtime: {exc}')
        return 1

    if not (len(plain) < len(state) and len(plain) < len(routing) and len(plain) < len(http)):
        print('web-runtime-pruning-runtime: plain Component loader was not smaller than feature runtimes')
        return 1

    print(
        'web-runtime-pruning-runtime: PASS '
        f'(plain={len(plain)}B state={len(state)}B routing={len(routing)}B http={len(http)}B)'
    )
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
