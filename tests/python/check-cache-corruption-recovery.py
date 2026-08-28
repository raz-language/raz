#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations
import argparse, os, shutil, subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

def run(cmd, cwd, env):
    return subprocess.run(cmd, cwd=cwd, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

def make_app(root: Path):
    (root/'src').mkdir(parents=True)
    (root/'raz.toml').write_text('[package]\nname="app"\nversion="1.0.0"\nkind="executable"\nentry="src/main.rz"\n\n[dependencies]\n\n[profile.release]\noptimization=2\ndebug=false\nincremental=true\n')
    (root/'src/main.rz').write_text('fn main() -> i64 { return 0; }\n')

def build(raz: Path, app: Path, env):
    return run([str(raz),'build','--release','--forge-native','--forge-structured-only'], app, env)

def main() -> int:
    ap=argparse.ArgumentParser(); ap.add_argument('--raz',required=True); ap.add_argument('--work-root',required=True); args=ap.parse_args()
    raz=Path(args.raz).resolve(); work=Path(args.work_root).resolve()
    if work.exists(): shutil.rmtree(work)
    env=os.environ.copy(); env['RAZ_HOME']=str(ROOT)
    runtime=ROOT/'build/release/src/runtime/libraz_runtime.a'
    if runtime.is_file(): env['RAZ_RUNTIME_LIBRARY']=str(runtime)

    # Cached executable corruption must never be restored as Fresh.
    app=work/'artifact'; make_app(app)
    first=build(raz,app,env)
    if first.returncode: print(first.stdout); return 1
    artifact=app/'target/cache/artifact.bin'; artifact.write_bytes(b'broken')
    second=build(raz,app,env)
    if second.returncode: print(second.stdout); return 1
    if 'Fresh app v1.0.0' in second.stdout:
        print('cache-corruption-recovery: corrupted artifact.bin was restored as Fresh'); return 1
    exe=app/'target/release/bin/app'
    if run([str(exe)],app,env).returncode != 0:
        print('cache-corruption-recovery: executable did not recover from artifact corruption'); return 1
    integrity=app/'target/cache/artifact.integrity'
    if not integrity.is_file() or len(integrity.read_text().split()) != 2:
        print('cache-corruption-recovery: artifact integrity record missing after recovery'); return 1

    # Truncated semantic/MIR/package metadata must be treated as a cache miss,
    # never as authoritative state. A changed program must still rebuild.
    cases=[('frontend.state','target/cache/frontend.state'),('mir.units','target/cache/mir.units'),('package units','target/release/packages/.units')]
    for label, rel in cases:
        app=work/label.replace(' ','-'); make_app(app)
        first=build(raz,app,env)
        if first.returncode: print(first.stdout); return 1
        (app/rel).write_text('truncated\n')
        (app/'src/main.rz').write_text('fn main() -> i64 { return 1; }\n')
        rebuilt=build(raz,app,env)
        if rebuilt.returncode: print(rebuilt.stdout); return 1
        if run([str(app/'target/release/bin/app')],app,env).returncode != 1:
            print(f'cache-corruption-recovery: {label} corruption preserved stale executable'); return 1
    print('cache-corruption-recovery: PASS (corrupt executable and truncated metadata recover safely)')
    return 0

if __name__=='__main__': raise SystemExit(main())
