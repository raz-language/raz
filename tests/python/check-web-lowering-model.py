#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PROJECT = (ROOT / "compiler/src/raz_driver/src/project.rz").read_text(encoding="utf-8")
DRIVER = (ROOT / "compiler/src/raz_driver/src/compiler_main.rz").read_text(encoding="utf-8")
WASM = (ROOT / "compiler/src/raz_codegen_wasm/src/wasm/codegen.rz").read_text(encoding="utf-8")
CLI = (ROOT / "compiler/src/raz_driver/src/cli.rz").read_text(encoding="utf-8")

checks = {
    "manifest has one canonical web classifier": "fn project_manifest_web_mode(" in PROJECT,
    "target=web mode is explicit": "if (is_web) { return 1; }" in PROJECT,
    "legacy reactive mode is explicit": "project_manifest_is_web(manifest_path, manifest_path_length)) { return 2; }" in PROJECT,
    "driver consumes one manifest mode": "web_mode = project_manifest_web_mode(" in DRIVER,
    "driver auto-detects Component main": "compiler_web_main_returns_component" in DRIVER and '"Component"' in DRIVER,
    "static target excludes application bundle": "if (web_target && !web_application)" in DRIVER,
    "application target uses shared web emitter": "if (web_application)" in DRIVER and "emit_web_application(" in DRIVER,
    "generic native package path excludes all web builds": "native_build.active && !web_build" in DRIVER,
    "wasm lowering has explicit mode": "global mut i64 wasm_browser_lowering_mode = 0;" in WASM,
    "wasm event roots are mode 1": "wasm_browser_lowering_mode == 1" in WASM,
    "wasm app roots are mode 2": "wasm_browser_lowering_mode == 2" in WASM,
    "reactive boolean inference removed": "wasm_browser_reactive_mode_enabled" not in WASM,
    "cli delegates target classification": "project_manifest_web_mode(manifest_path, manifest_path_length) == 1" in CLI,
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    for name in failed:
        print(f"web-lowering-model: FAIL: {name}")
    raise SystemExit(1)

print(f"web-lowering-model: PASS ({len(checks)} checks)")
