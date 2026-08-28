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

    project = work / "browser-api"
    created = run([str(raz), "new", str(project), "--target", "web"], work, env)
    if created.returncode != 0:
        print(created.stdout)
        return 1

    source = '''import web;

fn delayed() {
    web::set_text("status", "done");
}

fn inspect_event() {
    InputEvent input = current_input();
    KeyboardEvent keyboard = current_keyboard();
    PointerEvent pointer = current_pointer();
    Event event = current();
    input.value();
    input.checked();
    keyboard.key();
    keyboard.code();
    keyboard.repeat();
    keyboard.alt_key();
    keyboard.ctrl_key();
    keyboard.shift_key();
    keyboard.meta_key();
    pointer.client_x();
    pointer.client_y();
    pointer.button();
    web::set_value("field", "seen");
    web::exists("field");
    web::blur("field");
    event.stop_propagation();
    i64 timer = set_timeout("delayed", 5);
    if (timer < 0) { clear_timeout(timer); }
}

fn main() -> i64 {
    if (!web::prepare_dist()) { return 1; }
    Page page = Page::new("Browser API");
    page.input_id("field", "text", "field", "Type");
    page.p_id("status", "ready");
    page.browser_export("delayed");
    page.on("field", EventKind::KeyDown, "inspect_event");
    page.on("field", EventKind::PointerDown, "inspect_event");
    if (!page.write_route("/")) { return 1; }
    return 0;
}
'''
    (project / "src" / "main.rz").write_text(source, encoding="utf-8")

    built = run([str(raz), "build", "--release", "--forge-native", "--forge-structured-only"], project, env)
    if built.returncode != 0:
        print(built.stdout)
        return 1

    scripts = sorted((project / "dist" / "assets").glob("app.*.js"))
    wasms = sorted((project / "dist" / "assets").glob("app.*.wasm"))
    if len(scripts) != 1 or len(wasms) != 1:
        print("web-browser-api: expected one fingerprinted JS host and WASM module")
        return 1
    js = scripts[0].read_text(encoding="utf-8")
    for token in (
        "event_code_length", "event_repeat", "event_alt_key", "event_ctrl_key",
        "event_shift_key", "event_meta_key", "event_button", "event_client_x",
        "event_client_y", "event_stop_propagation", "dom_set_value", "dom_blur",
        "dom_click", "dom_exists", "timer_set_timeout", "timer_clear_timeout",
        "addEventListener('keydown'", "addEventListener('pointerdown'",
    ):
        if token not in js:
            print(f"web-browser-api: generated host missing {token}")
            return 1
    wasm_bytes = wasms[0].read_bytes()
    if wasm_bytes[:4] != b"\x00asm":
        print("web-browser-api: app.wasm is invalid")
        return 1
    if b"delayed" not in wasm_bytes or b"inspect_event" not in wasm_bytes:
        print("web-browser-api: browser-export/event roots are missing from WASM")
        return 1

    print("web-browser-api: PASS (typed events, DOM controls, and timers lower through Raz WASM ABI)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
