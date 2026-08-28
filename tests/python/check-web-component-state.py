#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations
import argparse, os, shutil, subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

def run(cmd: list[str], cwd: Path, env: dict[str, str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, cwd=cwd, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--raz', required=True)
    ap.add_argument('--work-root', required=True)
    args = ap.parse_args()
    raz = Path(args.raz).resolve()
    work = Path(args.work_root).resolve()
    if work.exists(): shutil.rmtree(work)
    work.mkdir(parents=True)
    project = work / 'component-state'
    shutil.copytree(ROOT / 'tests/examples/web/component-state', project, ignore=shutil.ignore_patterns('target', 'dist'))

    ui = (ROOT / 'library/web/ui/ui.rz').read_text(encoding='utf-8')
    required = [
        'fn state_scoped_hash(i64 scope, string key) -> i64',
        'public fn state_i64_scoped(i64 scope, string key, i64 initial) -> StateI64',
        'fn state_i64(Component& self, string key, i64 initial) -> StateI64',
        'const i64 WEB_STATE_CAPACITY = 512;',
        'const i64 WEB_EVENT_CAPACITY = 1024;',
    ]
    for item in required:
        if item not in ui:
            print(f'web-component-state: missing runtime contract: {item}')
            return 1

    source = (project / 'src/main.rz').read_text(encoding='utf-8')
    for item in ['component_scoped(Tag::Section, key)', 'card.state_i64("count", 0)', 'card.state_bool("enabled", true)', 'card.state_string("name", "Raz")']:
        if item not in source:
            print(f'web-component-state: fixture missing: {item}')
            return 1

    env = os.environ.copy()
    env['RAZ_HOME'] = str(ROOT)
    runtime = ROOT / 'build/release/src/runtime/libraz_runtime.a'
    if runtime.is_file(): env['RAZ_RUNTIME_LIBRARY'] = str(runtime)
    built = run([str(raz), 'build', '--release', 'raz.toml'], project, env)
    if built.returncode != 0:
        print(built.stdout)
        return 1

    assets = project / 'dist/assets'
    js_files = list(assets.glob('app.*.js'))
    wasm_files = list(assets.glob('app.*.wasm'))
    if len(js_files) != 1 or len(wasm_files) != 1:
        print('web-component-state: expected one fingerprinted JS and Wasm bundle')
        return 1
    js = js_files[0].read_text(encoding='utf-8')
    for item in ['applyStateBindings(changedSlot)', 'renderOwnedScope(scope)', 'data-raz-component-scope']:
        if item not in js:
            print(f'web-component-state: generated host missing: {item}')
            return 1
    node = subprocess.run(['node', '--check', str(js_files[0])], text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if node.returncode != 0:
        print(node.stdout); return 1
    wasm = subprocess.run(['node','-e',"const fs=require('fs');WebAssembly.compile(fs.readFileSync(process.argv[1])).then(()=>process.exit(0)).catch(e=>{console.error(e);process.exit(1)})",str(wasm_files[0])], text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if wasm.returncode != 0:
        print(wasm.stdout); return 1
    print('web-component-state: PASS (scoped local state + fast binding/component host)')
    return 0

if __name__ == '__main__': raise SystemExit(main())
