#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def run(cmd: list[str], cwd: Path, env: dict[str, str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, cwd=cwd, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--raz", required=True)
    ap.add_argument("--work-root", required=True)
    args = ap.parse_args()

    raz = Path(args.raz).resolve()
    work = Path(args.work_root).resolve()
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)

    env = os.environ.copy()
    env["RAZ_HOME"] = str(ROOT)
    runtime_library = ROOT / "build" / "release" / "src" / "runtime" / "libraz_runtime.a"
    if runtime_library.is_file():
        env["RAZ_RUNTIME_LIBRARY"] = str(runtime_library)

    project = work / "reactive-smoke"
    fixture = ROOT / "tests" / "examples" / "web" / "reactive"
    shutil.copytree(fixture, project)

    manifest = (project / "raz.toml").read_text(encoding="utf-8")
    source = (project / "src" / "main.rz").read_text(encoding="utf-8")
    if 'kind = "web"' not in manifest or '[web]' not in manifest or 'web = "raz:web"' not in manifest:
        print("web-reactive: fixture is missing advanced web manifest/dependency")
        return 1
    if "import web::ui;" not in source or "fn main() -> Component" not in source:
        print("web-reactive: fixture does not use the reactive Component API")
        return 1

    built = run([str(raz), "build", "--release", "raz.toml"], project, env)
    if built.returncode != 0:
        print(built.stdout)
        return 1

    index = project / "dist" / "index.html"
    js_candidates = sorted((project / "dist" / "assets").glob("app.*.js"))
    wasm_candidates = sorted((project / "dist" / "assets").glob("app.*.wasm"))
    css_candidates = sorted((project / "dist" / "assets").glob("app.*.css"))
    js = js_candidates[0] if len(js_candidates) == 1 else project / "dist" / "assets" / "missing.js"
    wasm = wasm_candidates[0] if len(wasm_candidates) == 1 else project / "dist" / "assets" / "missing.wasm"
    manifest = project / "dist" / "asset-manifest.json"
    if not index.is_file() or not js.is_file() or not wasm.is_file() or not manifest.is_file():
        print("web-reactive: release build did not produce fingerprinted index/JS/WASM/manifest")
        return 1
    html = index.read_text(encoding="utf-8")
    if js.name not in html:
        print("web-reactive: index does not reference fingerprinted JS")
        return 1
    if css_candidates and css_candidates[0].name not in html:
        print("web-reactive: index does not reference fingerprinted CSS")
        return 1
    if wasm.name not in js.read_text(encoding="utf-8"):
        print("web-reactive: JS does not reference fingerprinted WASM")
        return 1
    if list(project.rglob("robots.txt")):
        print("web-reactive: unexpected robots.txt output")
        return 1

    node_check = subprocess.run(["node", "--check", str(js)], text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if node_check.returncode != 0:
        print(node_check.stdout)
        return 1
    wasm_check = subprocess.run(
        ["node", "-e", "const fs=require('fs'); WebAssembly.compile(fs.readFileSync(process.argv[1])).then(()=>process.exit(0)).catch(e=>{console.error(e);process.exit(1)})", str(wasm)],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    if wasm_check.returncode != 0:
        print(wasm_check.stdout)
        return 1

    print("web-reactive: PASS (release Component bundle + fingerprinted JS/Wasm/CSS)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
