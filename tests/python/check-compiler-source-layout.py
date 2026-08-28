#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0
"""Regression checks for the canonical compiler source/package layout."""
from pathlib import Path
import sys
ROOT = Path(__file__).resolve().parents[2]
COMPILER = ROOT / "compiler"
SRC = COMPILER / "src"
EXPECTED = (
    "raz_lexer", "raz_parser", "raz_query", "raz_hir", "raz_mir", "raz_mir_opt", "raz_borrowck",
    "raz_codegen_forge", "raz_codegen_llvm", "raz_codegen_wasm",
    "raz_codegen_rxe", "raz_codegen_web", "raz_driver",
)
def fail(message: str) -> None:
    print(f"error: {message}", file=sys.stderr); raise SystemExit(1)
if not (SRC / "main.rz").is_file(): fail("compiler/src/main.rz is missing")
for sibling in COMPILER.glob("raz_*"):
    if sibling.is_dir(): fail(f"compiler package escaped compiler/src: {sibling.name}")
root_manifest=(COMPILER/"raz.toml").read_text(encoding="utf-8")
if 'driver = "src/raz_driver"' not in root_manifest:
    fail("compiler/raz.toml must depend on src/raz_driver")
for name in EXPECTED:
    package=SRC/name
    manifest=package/"raz.toml"
    entry=package/"src/lib.rz"
    if not manifest.is_file(): fail(f"missing package manifest: {manifest.relative_to(ROOT)}")
    if not entry.is_file(): fail(f"missing package entry: {entry.relative_to(ROOT)}")
    text=manifest.read_text(encoding="utf-8")
    if 'source = "src"' not in text or 'entry = "src/lib.rz"' not in text:
        fail(f"invalid package source layout: {manifest.relative_to(ROOT)}")
# Project loading must stop root recursive discovery at nested package manifests.
project=(SRC/"raz_driver/src/project.rz").read_text(encoding="utf-8")
for marker in ("fn project_path_crosses_nested_package(", "fn project_filter_nested_package_sources("):
    if marker not in project: fail(f"project loader missing nested-package boundary: {marker}")
bootstrap=(ROOT/"tools/bootstrap.py").read_text(encoding="utf-8")
for marker in ('source_root = root / "src"', 'compiler_project / "src" / "raz_driver"'):
    if marker not in bootstrap: fail(f"bootstrap is not aligned with compiler/src: {marker}")
print("compiler source/package layout: PASS (all compiler packages under compiler/src)")
