#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations
import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess


def run(args: list[str]) -> str:
    proc = subprocess.run(args, text=True, capture_output=True)
    if proc.returncode != 0:
        raise SystemExit(f"command failed: {' '.join(args)}\n{proc.stdout}\n{proc.stderr}")
    return proc.stdout + proc.stderr


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--raz', required=True)
    parser.add_argument('--fixture', required=True)
    parser.add_argument('--work', required=True)
    args = parser.parse_args()

    work = Path(args.work)
    if work.exists(): shutil.rmtree(work)
    shutil.copytree(args.fixture, work)
    shutil.rmtree(work / '.raz', ignore_errors=True)
    shutil.rmtree(work / 'target', ignore_errors=True)

    base = [args.raz, 'build', str(work), '--target', 'host', '--color', 'never']
    run(base + ['--force'])

    workspace = work / '.raz/cache/workspace-v1.state'
    if not workspace.is_file(): raise SystemExit('missing persistent workspace graph')
    ws = workspace.read_text()
    edge = 'edge "hello-package@0.1.0::main" "hello-package@0.1.0::util::math"'
    if edge not in ws: raise SystemExit('missing main -> util::math workspace edge')

    cache = work / '.raz/cache/host/debug'
    for stem in ('main', 'util__math'):
        stage = cache / f'{stem}.incremental'
        if not stage.is_file(): raise SystemExit(f'missing stage cache: {stage}')
        text = stage.read_text()
        for field in ('source=', 'imports=', 'interface=', 'semantic=', 'hir=', 'mir=', 'forge_ir=', 'build='):
            if field not in text: raise SystemExit(f'missing {field} in {stage}')

    native = work / 'target/host/debug/native/modules'
    main_obj = native / ('main.obj' if __import__('os').name == 'nt' else 'main.o')
    util_obj = native / ('util__math.obj' if __import__('os').name == 'nt' else 'util__math.o')
    if not main_obj.is_file() or not util_obj.is_file(): raise SystemExit('missing per-module native objects')
    main_before, util_before = digest(main_obj), digest(util_obj)

    fresh = run(base + ['--verbose'])
    if '0 compiled, 2 fresh' not in fresh or 'native link' not in fresh or 'Fresh' not in fresh:
        raise SystemExit(f'fresh build did not reuse modules/link:\n{fresh}')

    # Force one native object through regeneration without changing its FIR. The
    # object bytes are deterministic, so the content-addressed link cache must
    # preserve both the object and final executable timestamps and skip linking.
    util_state = native / 'util__math.object.fingerprint'
    if not util_state.is_file(): raise SystemExit('missing util native object cache state')
    artifact = work / 'target/host/debug' / ('hello-package.exe' if __import__('os').name == 'nt' else 'hello-package')
    if not artifact.is_file(): raise SystemExit('missing native executable')
    object_stamp_before = util_obj.stat().st_mtime_ns
    artifact_stamp_before = artifact.stat().st_mtime_ns
    artifact_before = digest(artifact)
    util_state.unlink()
    regenerated = run(base + ['--verbose'])
    if 'Fresh' not in regenerated or 'native link' not in regenerated:
        raise SystemExit(f'byte-identical object regeneration unnecessarily relinked:\n{regenerated}')
    if digest(util_obj) != util_before or util_obj.stat().st_mtime_ns != object_stamp_before:
        raise SystemExit('byte-identical object regeneration replaced the cached object')
    if digest(artifact) != artifact_before or artifact.stat().st_mtime_ns != artifact_stamp_before:
        raise SystemExit('byte-identical object regeneration replaced/relinked the executable')
    state_text = util_state.read_text()
    if 'raz-native-object-v3' not in state_text or 'input=' not in state_text or 'object=' not in state_text:
        raise SystemExit('native object cache did not persist v3 input/content fingerprints')

    util = work / 'src/util/math.rz'
    util.write_text(util.read_text() + '\nfn private_helper(i64 value) -> i64 { return value + 1; }\n')
    private = run(base + ['--verbose'])
    if '1 compiled, 1 fresh' not in private:
        raise SystemExit(f'private implementation edit invalidated too much:\n{private}')
    if digest(main_obj) != main_before: raise SystemExit('unaffected main object was rebuilt/changed')
    if digest(util_obj) == util_before: raise SystemExit('changed util object was not refreshed')

    util.write_text(util.read_text().replace('fn answer() -> i64', 'public fn answer() -> i64', 1))
    public = run(base + ['--verbose'])
    if '2 compiled, 0 fresh' not in public:
        raise SystemExit(f'public interface edit did not invalidate dependent:\n{public}')

    print('three-pass-incremental: PASS (workspace graph, staged fingerprints, precise invalidation, content-addressed object/link cache)')
    return 0


if __name__ == '__main__': raise SystemExit(main())
