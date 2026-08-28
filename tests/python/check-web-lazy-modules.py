#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations
import argparse, os, re, shutil, subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "tests/examples/web/lazy-modules"

def run(cmd, cwd, env):
    return subprocess.run(cmd, cwd=cwd, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--raz", required=True)
    ap.add_argument("--work-root", required=True)
    args = ap.parse_args()
    raz = Path(args.raz).resolve()
    work = Path(args.work_root).resolve()
    if work.exists(): shutil.rmtree(work)
    shutil.copytree(FIXTURE, work)
    env = os.environ.copy(); env["RAZ_HOME"] = str(ROOT)
    runtime = ROOT / "build/release/src/runtime/libraz_runtime.a"
    if runtime.is_file(): env["RAZ_RUNTIME_LIBRARY"] = str(runtime)
    result = run([str(raz), "build", "--release", "--forge-native", "--forge-structured-only"], work, env)
    if result.returncode:
        print(result.stdout); return 1
    dist = work / "dist"
    html = (dist / "index.html").read_text(encoding="utf-8")
    js_files = sorted((dist / "assets").glob("app.*.js"))
    wasm_files = list(dist.rglob("*.wasm"))
    chunks = sorted((dist / "assets/chunks").glob("editor.*.mjs"))
    if len(js_files) != 1 or len(chunks) != 1:
        print("web-lazy-modules: expected one fingerprinted loader and one chunk"); return 1
    if wasm_files:
        print("web-lazy-modules: client-only lazy module unexpectedly emitted WASM"); return 1
    js = js_files[0].read_text(encoding="utf-8")
    chunk_rel = "/assets/chunks/" + chunks[0].name
    if "void import('" + chunk_rel + "')" not in js:
        print("web-lazy-modules: lazy import was not rewritten to fingerprinted chunk", js); return 1
    if "WebAssembly.instantiate" in js or "app.wasm" in js:
        print("web-lazy-modules: client-only loader still contains WASM host bootstrap"); return 1
    if "{ once: true }" not in js:
        print("web-lazy-modules: lazy module event is not one-shot"); return 1
    if not re.search(r'assets/app\.[0-9a-f]{16}\.js', html):
        print("web-lazy-modules: HTML does not reference fingerprinted loader"); return 1
    if (dist / "assets/chunks/editor.mjs").exists():
        print("web-lazy-modules: canonical chunk was not removed in release output"); return 1
    print("web-lazy-modules: PASS (fingerprinted on-demand ES module, no app.wasm)")
    return 0

if __name__ == "__main__": raise SystemExit(main())
