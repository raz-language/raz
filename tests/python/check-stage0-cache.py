#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Static contract for the persistent Stage-0 bootstrap cache."""
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[2]
bootstrap = (root / "tools" / "bootstrap.py").read_text(encoding="utf-8")
example = (root / "bootstrap.example.toml").read_text(encoding="utf-8")
readme = (root / "src" / "bootstrap" / "README.md").read_text(encoding="utf-8")

checks = {
    "complete cache probe exists": "def cached_stage0_artifacts(" in bootstrap,
    "cache includes Stage-0 driver": '"driver": [f"raz-stage0{EXE}"]' in bootstrap,
    "cache includes compatibility compiler": '"compat": [f"razc-stage0{EXE}"]' in bootstrap,
    "cache includes runtime": '"runtime": ["raz_runtime.lib", "libraz_runtime.a"]' in bootstrap,
    "cache includes Forge bridge": '"bridge": ["raz_forge_bridge.lib", "libraz_forge_bridge.a"]' in bootstrap,
    "cache includes Forge": '"forge": ["forge.lib", "libforge.a"]' in bootstrap,
    "Windows cache includes ObLink": 'required["oblink"] = [f"oblink{EXE}"]' in bootstrap,
    "cache hit skips build branch": "Stage-0 cache: hit" in bootstrap and "if cached_stage0 is not None:" in bootstrap,
    "explicit rebuild flag exists": '"--rebuild-stage0"' in bootstrap,
    "clean removes host cache": "shutil.rmtree(host_build, ignore_errors=True)" in bootstrap,
    "config documents rebuild": "rebuild-stage0 = false" in example,
    "README documents cache": "## Stage-0 cache" in readme,
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    print("stage0-cache: FAIL")
    for name in failed:
        print(f"  - {name}")
    sys.exit(1)
print(f"stage0-cache: PASS ({len(checks)} contracts)")
