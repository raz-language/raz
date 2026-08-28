#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations
import argparse, os, shutil, subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--raz", required=True)
    ap.add_argument("--work-root", required=True)
    args = ap.parse_args()
    work = Path(args.work_root).resolve()
    if work.exists(): shutil.rmtree(work)
    (work / "src").mkdir(parents=True)
    (work / "raz.toml").write_text('''[package]\nname="web-static-component-guard"\nversion="0.1.0"\nkind="executable"\nsource="src"\nentry="src/main.rz"\n\n[build]\ntarget="web"\n\n[dependencies]\nweb="raz:web"\n''', encoding="utf-8")
    (work / "src/main.rz").write_text('''import web;\nimport web::ui;\n\nfn main() -> i64 {\n    StateI64 count = state_i64("count", 1);\n    Component page = component(Tag::Main);\n    page.text_i64_state(Tag::P, "Count: ", &count);\n    if (web::write_component_route("/", "Guard", &mut page)) { return 0; }\n    return 1;\n}\n''', encoding="utf-8")
    env = os.environ.copy(); env["RAZ_HOME"] = str(ROOT)
    runtime = ROOT / "build/release/src/runtime/libraz_runtime.a"
    if runtime.is_file(): env["RAZ_RUNTIME_LIBRARY"] = str(runtime)
    p = subprocess.run([str(Path(args.raz).resolve()), "build", "--release", "--forge-native", "--forge-structured-only"], cwd=work, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if p.returncode == 0:
        print("web-static-component-guard: browser-bound component was accepted by static-only route helper")
        return 1
    if (work / "dist/app.wasm").exists() or (work / "dist/app.js").exists():
        print("web-static-component-guard: rejected static component still emitted browser runtime")
        return 1
    print("web-static-component-guard: PASS (browser-bound Component cannot silently enter runtime-free static path)")
    return 0

if __name__ == "__main__": raise SystemExit(main())
