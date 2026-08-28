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
STATIC = ROOT / "tests/examples/web/routing-static"
REACTIVE = ROOT / "tests/examples/web/routing-reactive"


def run(cmd, cwd, env):
    return subprocess.run(cmd, cwd=cwd, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)


def build_fixture(raz: Path, source: Path, dest: Path, env: dict[str, str]) -> tuple[bool, str]:
    if dest.exists():
        shutil.rmtree(dest)
    shutil.copytree(source, dest)
    result = run([str(raz), "build", "--release", "--forge-native", "--forge-structured-only"], dest, env)
    return result.returncode == 0, result.stdout


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
    runtime = ROOT / "build/release/src/runtime/libraz_runtime.a"
    if runtime.is_file():
        env["RAZ_RUNTIME_LIBRARY"] = str(runtime)

    static_work = work / "static"
    ok, output = build_fixture(raz, STATIC, static_work, env)
    if not ok:
        print(output)
        return 1

    expected = [
        static_work / "dist/index.html",
        static_work / "dist/blog/2026/hello/index.html",
        static_work / "dist/docs/guides/install/index.html",
        static_work / "dist/404.html",
        static_work / "dist/old-docs/index.html",
    ]
    missing = [str(path.relative_to(static_work)) for path in expected if not path.is_file()]
    if missing:
        print("web-routing: missing static route artifacts:")
        for path in missing:
            print("  ", path)
        return 1
    if (static_work.parent / "escape").exists() or (static_work / "escape").exists():
        print("web-routing: traversal route escaped dist")
        return 1
    home_html = (static_work / "dist/index.html").read_text(encoding="utf-8")
    if '<div data-raz-layout="site" class="site-layout">' not in home_html:
        print("web-routing: static layout marker missing")
        return 1
    redirect = (static_work / "dist/old-docs/index.html").read_text(encoding="utf-8")
    if 'http-equiv="refresh" content="0;url=/docs/guides/install"' not in redirect or 'href="/docs/guides/install"' not in redirect:
        print("web-routing: redirect page is missing target metadata/fallback link")
        return 1
    if list((static_work / "dist").glob("app*.js")) or list((static_work / "dist").glob("app*.wasm")):
        print("web-routing: static routing unexpectedly emitted browser runtime")
        return 1

    reactive_work = work / "reactive"
    ok, output = build_fixture(raz, REACTIVE, reactive_work, env)
    if not ok:
        print(output)
        return 1
    js_candidates = sorted((reactive_work / "dist/assets").glob("app.*.js"))
    wasm_candidates = sorted((reactive_work / "dist/assets").glob("app.*.wasm"))
    js = js_candidates[0] if len(js_candidates) == 1 else reactive_work / "dist/assets/missing.js"
    wasm = wasm_candidates[0] if len(wasm_candidates) == 1 else reactive_work / "dist/assets/missing.wasm"
    if not js.is_file() or not wasm.is_file():
        print("web-routing: reactive routing did not emit fingerprinted application assets")
        return 1
    node = shutil.which("node")
    if node is None:
        print("web-routing: node is required for WebAssembly validation")
        return 1
    validate = subprocess.run(
        [node, "-e", "const fs=require('fs');const b=fs.readFileSync(process.argv[1]);if(!WebAssembly.validate(b)){process.exit(1)}", str(wasm)],
        cwd=reactive_work, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
    )
    if validate.returncode != 0:
        print("web-routing: generated WebAssembly failed engine validation")
        print(validate.stdout)
        return 1
    js_text = js.read_text(encoding="utf-8")
    required_js = ["setRoute(browserRoute())", "popstate", "data-raz-nav", "history.pushState"]
    missing_js = [item for item in required_js if item not in js_text]
    if missing_js:
        print("web-routing: generated router host is missing navigation hooks:")
        for item in missing_js:
            print("  ", item)
        return 1

    web_lib = (ROOT / "library/web/src/lib.rz").read_text(encoding="utf-8")
    required_static_api = [
        "public fn route_path(string pattern) -> String",
        "public fn route_bind(String&mut path, string name, string value) -> bool",
        "public fn route_bind_splat(String&mut path, string name, string value) -> bool",
        "public fn write_route_path(Page&mut self, String& route) -> bool",
        "public fn write_component_route_path(String& route, string title",
        "if (first == 58 || first == 42) { return false; }",
    ]
    missing_static_api = [item for item in required_static_api if item not in web_lib]
    if missing_static_api:
        print("web-routing: static prerender API invariant missing:")
        for item in missing_static_api:
            print("  ", item)
        return 1

    ui = (ROOT / "library/web/ui/ui.rz").read_text(encoding="utf-8")
    required_api = [
        "public fn route_match(string pattern) -> bool",
        "public fn route_param(string pattern, string name) -> String",
        "public fn route_splat(string pattern, string name) -> String",
        "pl >= 1 && raz_rt_load_u8(pattern_data + ps) == 42",
        "route_mark_structural_scope(0);",
    ]
    missing_api = [item for item in required_api if item not in ui]
    if missing_api:
        print("web-routing: route API invariant missing:")
        for item in missing_api:
            print("  ", item)
        return 1

    print("web-routing: PASS (dynamic static prerender + 404/redirect + reactive params/named splats/navigation)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
