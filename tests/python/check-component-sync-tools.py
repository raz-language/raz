#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Regression checks for standalone/embedded component synchronization tools."""

from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def load_tool(name: str, filename: str):
    spec = spec_from_file_location(name, ROOT / "tools" / filename)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"could not load {filename}")
    module = module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


checker = load_tool("raz_component_checker", "check-embedded-components.py")
syncer = load_tool("raz_component_syncer", "sync-embedded-components.py")

with tempfile.TemporaryDirectory(prefix="raz-component-sync-") as temporary:
    root = Path(temporary)
    standalone = root / "standalone"
    embedded = root / "embedded"

    (standalone / "src" / "target").mkdir(parents=True)
    (standalone / "target").mkdir()
    (standalone / "build").mkdir()
    (standalone / "CMakeLists.txt").write_text("project(component)\n", encoding="utf-8")
    (standalone / "src" / "target" / "abi.cpp").write_text("// ABI source\n", encoding="utf-8")
    (standalone / "target" / "cache.bin").write_bytes(b"generated")
    (standalone / "build" / "output.o").write_bytes(b"generated")

    embedded.mkdir()
    (embedded / "stale.cpp").write_text("// stale\n", encoding="utf-8")

    syncer.replace_component("Fixture", standalone, embedded)

    assert (embedded / "src" / "target" / "abi.cpp").is_file(), (
        "nested src/target is maintained source and must never be filtered as build output"
    )
    assert not (embedded / "target").exists(), "repository-root target/ must remain ignored"
    assert not (embedded / "build").exists(), "repository-root build/ must remain ignored"
    assert not (embedded / "stale.cpp").exists(), "synchronization must remove embedded-only stale files"
    assert checker.compare_component("Fixture", standalone, embedded) == [], (
        "checker and synchronizer must agree on the exact maintained source set"
    )

print("component-sync-tools: PASS (nested source target preserved; root build metadata ignored)")
