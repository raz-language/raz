#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations
import argparse, os, shutil, subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "tests/examples/web/static-components"

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--raz", required=True)
    ap.add_argument("--work-root", required=True)
    args = ap.parse_args()
    work = Path(args.work_root).resolve()
    if work.exists(): shutil.rmtree(work)
    shutil.copytree(FIXTURE, work)
    env = os.environ.copy(); env["RAZ_HOME"] = str(ROOT)
    runtime = ROOT / "build/release/src/runtime/libraz_runtime.a"
    if runtime.is_file(): env["RAZ_RUNTIME_LIBRARY"] = str(runtime)
    p = subprocess.run([str(Path(args.raz).resolve()), "build", "--release", "--forge-native", "--forge-structured-only"], cwd=work, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if p.returncode != 0:
        print(p.stdout); return 1
    index = work / "dist/index.html"; about = work / "dist/about/index.html"
    if not index.is_file() or not about.is_file():
        print("web-static-components: missing static route output"); return 1
    html = index.read_text(encoding="utf-8")
    if '<section class="hero">' not in html or "Static components in Raz" not in html:
        print("web-static-components: component HTML missing"); return 1
    css = list((work / "dist/assets").glob("app.*.css"))
    if len(css) != 1 or ".hero{max-width:64rem;margin:3rem auto;padding:2rem;}" not in css[0].read_text(encoding="utf-8"):
        print("web-static-components: component CSS missing"); return 1
    assets = work / "dist/assets"
    if list(assets.glob("*.js")) or list(assets.glob("*.wasm")) or list(work.glob("app*.js")) or list(work.glob("app*.wasm")):
        print("web-static-components: static component route unexpectedly emitted browser runtime"); return 1
    if '<html lang="en-US">' not in about.read_text(encoding="utf-8"):
        print("web-static-components: language route helper missing"); return 1
    print("web-static-components: PASS (reusable Component trees -> plain HTML/CSS, zero JS/WASM)")
    return 0

if __name__ == "__main__": raise SystemExit(main())
