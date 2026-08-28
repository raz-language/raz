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

    project = work / "web-interactive"
    created = run([str(raz), "new", str(project), "--target", "web"], work, env)
    if created.returncode != 0:
        print(created.stdout)
        return 1

    source = '''import web;

global mut i64 count = 0;

fn increment() {
    count += 1;
    web::set_text_i64("count", count);
}

fn home() -> bool {
    Page page = Page::new("Interactive");
    page.p_id("count", "0");
    page.button_id("inc", "Increment");
    page.on_click("inc", "increment");
    return page.write_route("/");
}

fn about() -> bool {
    Page page = Page::new("Static");
    page.p("Static page");
    return page.write_route("/about");
}

fn main() -> i64 {
    if (!web::prepare_dist()) { return 1; }
    if (!home() || !about()) { return 1; }
    return 0;
}
'''
    (project / "src" / "main.rz").write_text(source, encoding="utf-8")

    built = run([str(raz), "build", "--release", "--forge-native", "--forge-structured-only"], project, env)
    if built.returncode != 0:
        print(built.stdout)
        return 1

    root_html = project / "dist" / "index.html"
    root_js_candidates = sorted((project / "dist" / "assets").glob("app.*.js"))
    root_wasm_candidates = sorted((project / "dist" / "assets").glob("app.*.wasm"))
    root_js = root_js_candidates[0] if len(root_js_candidates) == 1 else project / "dist" / "assets" / "missing-app.js"
    root_wasm = root_wasm_candidates[0] if len(root_wasm_candidates) == 1 else project / "dist" / "assets" / "missing-app.wasm"
    manifest = project / "dist" / "asset-manifest.json"
    about_html = project / "dist" / "about" / "index.html"
    about_js = list((project / "dist" / "about").glob("app*.js"))
    if not root_html.is_file() or not root_js.is_file() or not root_wasm.is_file() or not manifest.is_file() or not about_html.is_file():
        print("web-interactive: expected fingerprinted release files are missing")
        return 1
    if about_js:
        print("web-interactive: static route unexpectedly emitted JavaScript")
        return 1

    html = root_html.read_text(encoding="utf-8")
    js = root_js.read_text(encoding="utf-8")
    static_html = about_html.read_text(encoding="utf-8")
    if f'<script type="module" src="./assets/{root_js.name}"></script>' not in html:
        print("web-interactive: interactive page does not reference fingerprinted module")
        return 1
    if f"/assets/{root_wasm.name}" not in js:
        print("web-interactive: generated module does not reference fingerprinted WASM")
        return 1
    if "addEventListener('click'" not in js or "rz['increment']" not in js:
        print("web-interactive: generated host shim is missing event-to-WASM dispatch")
        return 1
    forbidden_logic = ["count +=", "textContent = value +", "let value ="]
    if any(token in js for token in forbidden_logic):
        print("web-interactive: application behavior leaked into JavaScript")
        return 1
    if root_wasm.read_bytes()[:4] != b"\x00asm":
        print("web-interactive: app.wasm is not a WebAssembly binary")
        return 1
    if (project / "dist" / ".raz-wasm-required").exists():
        print("web-interactive: private WASM requirement marker leaked into dist output")
        return 1
    if "<script" in static_html:
        print("web-interactive: static page unexpectedly references JavaScript")
        return 1

    print("web-interactive: PASS (ordinary Raz event function -> WASM; JS is host glue only)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
