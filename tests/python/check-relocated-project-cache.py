#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations
import argparse, os, shutil, subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

def run(cmd, cwd, env):
    return subprocess.run(cmd, cwd=cwd, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--raz', required=True)
    ap.add_argument('--work-root', required=True)
    args = ap.parse_args()
    raz = Path(args.raz).resolve()
    work = Path(args.work_root).resolve()
    if work.exists(): shutil.rmtree(work)

    original = work / 'original'
    moved = work / 'moved'
    for root in (original,):
        (root/'dep/src').mkdir(parents=True)
        (root/'app/src').mkdir(parents=True)
    (original/'dep/raz.toml').write_text('[package]\nname="dep"\nversion="1.0.0"\nkind="static-library"\nentry="src/lib.rz"\n\n[dependencies]\n')
    (original/'dep/src/lib.rz').write_text('namespace dep;\npublic fn value() -> i64 { return 1; }\n')
    (original/'app/raz.toml').write_text('[package]\nname="app"\nversion="1.0.0"\nkind="executable"\nentry="src/main.rz"\n\n[dependencies]\ndep="../dep"\n\n[profile.release]\noptimization=2\ndebug=false\nincremental=true\n')
    (original/'app/src/main.rz').write_text('import dep;\nfn main() -> i64 { return dep::value() - 1; }\n')

    env = os.environ.copy(); env['RAZ_HOME'] = str(ROOT)
    runtime = ROOT/'build/release/src/runtime/libraz_runtime.a'
    if runtime.is_file(): env['RAZ_RUNTIME_LIBRARY'] = str(runtime)
    first = run([str(raz),'build','--release','--forge-native','--forge-structured-only'], original/'app', env)
    if first.returncode != 0:
        print(first.stdout); return 1
    if run([str(original/'app/target/release/bin/app')], original/'app', env).returncode != 0:
        print('relocated-project-cache: initial executable mismatch'); return 1

    shutil.copytree(original, moved)
    source = moved/'app/src/main.rz'
    source.write_text('import dep;\nfn main() -> i64 { return 2 - dep::value(); }\n')
    rebuilt = run([str(raz),'build','--release','--forge-native','--forge-structured-only'], moved/'app', env)
    if rebuilt.returncode != 0:
        print(rebuilt.stdout); return 1
    if 'Fresh app v1.0.0' in rebuilt.stdout:
        print('relocated-project-cache: relocated stale artifact was declared Fresh')
        print(rebuilt.stdout); return 1
    exe = moved/'app/target/release/bin/app'
    if run([str(exe)], moved/'app', env).returncode != 1:
        print('relocated-project-cache: relocated build reused stale pre-move source snapshot')
        print(rebuilt.stdout); return 1
    cached = (moved/'app/target/cache/project.source').read_text(encoding='utf-8')
    if str(original) in cached or str(moved) not in cached:
        print('relocated-project-cache: project snapshot did not rebind to moved workspace')
        return 1
    print('relocated-project-cache: PASS (warm target/ cannot restore source snapshots from the old workspace)')
    return 0

if __name__ == '__main__': raise SystemExit(main())
