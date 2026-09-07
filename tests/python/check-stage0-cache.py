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
build_driver = (root / "src" / "bootstrap" / "tools" / "raz" / "detail" / "build_driver.hpp").read_text(encoding="utf-8")

checks = {
    "complete cache probe exists": "def cached_stage0_artifacts(" in bootstrap,
    "cache includes Stage-0 driver": '"driver": [f"raz-stage0{EXE}"]' in bootstrap,
    "cache includes compatibility compiler": '"compat": [f"razc-stage0{EXE}"]' in bootstrap,
    "cache includes runtime": '"runtime": ["raz_runtime.lib", "libraz_runtime.a"]' in bootstrap,
    "cache includes Forge bridge": '"bridge": ["raz_forge_bridge.lib", "libraz_forge_bridge.a"]' in bootstrap,
    "cache includes Forge": '"forge": ["forge.lib", "libforge.a"]' in bootstrap,
    "Windows cache includes ObLink": 'required["oblink"] = [f"oblink{EXE}"]' in bootstrap,
    "cache hit skips build branch": "Stage-0 cache: hit" in bootstrap and "if cached_stage0 is not None:" in bootstrap,
    "CMake metadata location is still detected": 'read_cache(host_build, "CMAKE_HOME_DIRECTORY", required=False)' in bootstrap and 'read_cache(host_build, "CMAKE_CACHEFILE_DIR", required=False)' in bootstrap,
    "portable cache has source digest": "def _stage0_source_digest(" in bootstrap and "STAGE0_PORTABLE_DIGEST" in bootstrap,
    "relocated cache accepts matching portable artifacts": "stage0_portable_cache_matches_sources(host_build)" in bootstrap,
    "portable runtime dependencies are staged": "def staged_runtime_link_dependencies(" in bootstrap,
    "explicit rebuild flag exists": '"--rebuild-stage0"' in bootstrap,
    "stale relocated cache is regenerated": "Relocated Stage-0 artifacts are stale or predate the portable cache contract" in bootstrap,
    "current cache writes portable marker": "write_stage0_portable_cache_marker(host_build)" in bootstrap,
    "Stage-0 resolves installed runtime relative to executable": 'staged_toolchain_library("libraz_runtime.a")' in build_driver or 'staged_toolchain_library("raz_runtime.lib"' in build_driver,
    "Stage-0 resolves installed Forge bridge relative to executable": "stage0_forge_bridge_library()" in build_driver,
    "Stage-0 resolves installed Forge relative to executable": "stage0_forge_library()" in build_driver,
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
