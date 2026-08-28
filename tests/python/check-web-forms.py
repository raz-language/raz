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
STATIC = ROOT / "tests/examples/web/forms-static"
REACTIVE = ROOT / "tests/examples/web/forms-reactive"


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
    html = (static_work / "dist/index.html").read_text(encoding="utf-8")
    required = [
        '<form action="/contact" method="post" class="contact-form">',
        '<input type="hidden" name="csrf" value="a&amp;b">',
        '<select name="plan" class="plan-select">',
        '<option value="pro" selected>Pro</option>',
        '<input type="checkbox" name="terms" value="yes" checked>',
        '<input type="radio" name="tier" value="pro" checked>',
        'Hello &lt;Raz&gt;',
        'role="alert" aria-live="polite"',
        'enctype="multipart/form-data"',
        'accept=".txt,image/*"',
        '<input type="range" name="experience" min="0" max="10" step="1">',
        '<input type="datetime-local" name="appointment" placeholder="">',
        '<input type="color" name="accent" placeholder="">',
    ]
    missing = [item for item in required if item not in html]
    if missing:
        print("web-forms: static output missing:")
        for item in missing:
            print("  ", item)
        return 1
    if list((static_work / "dist").glob("app*.js")) or list((static_work / "dist").glob("app*.wasm")):
        print("web-forms: static progressive form unexpectedly emitted browser runtime")
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
        print("web-forms: reactive form did not emit fingerprinted app assets")
        return 1

    ui = (ROOT / "library/web/ui/ui.rz").read_text(encoding="utf-8")
    invariants = [
        "InputType::File",
        "InputType::Hidden",
        "InputType::Range",
        "InputType::DateTimeLocal",
        "fn enctype(Element&mut self, string value) -> bool",
        "fn accept(Element&mut self, string value) -> bool",
        "fn pattern(Element&mut self, string value) -> bool",
        "fn min(Element&mut self, string value) -> bool",
        "fn max(Element&mut self, string value) -> bool",
        "fn step(Element&mut self, string value) -> bool",
        "fn input_mode(Element&mut self, string value) -> bool",
        "fn multiple(Element&mut self) -> bool",
        "fn selected(Element&mut self) -> bool",
        "self.tag != Tag::Input && self.tag != Tag::Textarea && self.tag != Tag::Select",
    ]
    missing = [item for item in invariants if item not in ui]
    if missing:
        print("web-forms: reactive API invariant missing:")
        for item in missing:
            print("  ", item)
        return 1

    print("web-forms: PASS (static progressive forms + richer HTML5 controls/constraints + multipart/file/select)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
