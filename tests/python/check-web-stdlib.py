#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import tomllib

ROOT = Path(__file__).resolve().parents[2]
WEB = ROOT / "library" / "web"

manifest = tomllib.loads((WEB / "raz.toml").read_text(encoding="utf-8"))
deps = manifest.get("dependencies", {})
if "std" in deps:
    raise SystemExit("web-stdlib: native std dependency is forbidden")

required = [
    WEB / "std" / "browser.rz",
    WEB / "std" / "events.rz",
    WEB / "std" / "dom.rz",
    WEB / "std" / "timers.rz",
    WEB / "build" / "fs.rz",
    WEB / "build" / "format.rz",
    WEB / "source-order.txt",
]
missing = [str(path.relative_to(ROOT)) for path in required if not path.is_file()]
if missing:
    raise SystemExit("web-stdlib: missing " + ", ".join(missing))

browser_sources = "\n".join(path.read_text(encoding="utf-8") for path in (WEB / "std").glob("*.rz"))
for forbidden in ("std::fs", "std::io", "std::thread", "std::net", "std::process"):
    if forbidden in browser_sources:
        raise SystemExit(f"web-stdlib: forbidden native API in browser stdlib: {forbidden}")

print("web-stdlib: PASS (browser stdlib is isolated from native std)")
