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

    project = work / "web-smoke"
    created = run([str(raz), "new", str(project), "--web"], work, env)
    if created.returncode != 0:
        print(created.stdout)
        return 1

    manifest = (project / "raz.toml").read_text(encoding="utf-8")
    source = (project / "src" / "main.rz").read_text(encoding="utf-8")
    if '[build]\ntarget = "web"' not in manifest or 'web = "raz:web"' not in manifest:
        print("web-static: generated manifest is missing web target/toolchain dependency")
        return 1
    if "Page::new" not in source or "Component content = main_component();" not in source or "page.component(&mut content)" not in source or 'write_route("/")' not in source:
        print("web-static: starter source does not use the component-first static page API")
        return 1

    # The canonical web starter must itself prove the static-first contract:
    # a fresh project builds to ordinary hostable files without JS/WASM.
    public = project / "public"
    starter_css = public / "styles.css"
    starter_favicon = public / "favicon.svg"
    if not starter_css.is_file() or not starter_favicon.is_file():
        print("web-static: --web scaffold is missing default public assets")
        return 1
    if 'page.stylesheet("/styles.css")' not in source or 'page.icon("/favicon.svg")' not in source:
        print("web-static: starter source is not wired to generated public assets")
        return 1

    (public / "site-extra.txt").write_text("copied\n", encoding="utf-8")
    built = run([str(raz), "build", "--release", "--forge-native", "--forge-structured-only"], project, env)
    if built.returncode != 0:
        print(built.stdout)
        return 1

    index = project / "dist" / "index.html"
    css = project / "dist" / "styles.css"
    wasm = project / "dist" / "app.wasm"
    marker = project / "dist" / ".raz-wasm-required"
    favicon = project / "dist" / "favicon.svg"
    extra = project / "dist" / "site-extra.txt"
    if not index.is_file() or not css.is_file() or not favicon.is_file() or not extra.is_file():
        print("web-static: build did not produce static dist/ output")
        return 1
    if wasm.exists() or marker.exists():
        print("web-static: static page unexpectedly emitted browser WASM metadata/artifacts")
        return 1
    html = index.read_text(encoding="utf-8")
    if (project / "dist" / "app.js").exists() or "<script" in html:
        print("web-static: static starter unexpectedly emitted JavaScript")
        return 1
    if "<!doctype html>" not in html or "Hello from Raz" not in html or "/styles.css" not in html or "/favicon.svg" not in html:
        print("web-static: generated HTML does not contain expected document/page content")
        return 1

    print("web-static: PASS (raz new --web -> release dist/ with HTML/public assets only; no JS or WASM)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
