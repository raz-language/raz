#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Verify the self-host compiler uses semantic modules in production.

The bootstrap order is retained only as frozen seed metadata. Production package
builds must not contain compiler/source-order.txt, because its presence selects
legacy ordered-compilation-unit behavior in the project driver.
"""
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[1]
compiler = root / "compiler"
source_root = compiler / "src"
legacy_order = compiler / "source-order.txt"
bootstrap_order = compiler / "bootstrap-source-order.txt"

if legacy_order.exists():
    print("compiler-semantic-modules: FAIL: compiler/source-order.txt selects legacy concatenation")
    sys.exit(1)
if not bootstrap_order.is_file():
    print("compiler-semantic-modules: FAIL: missing compiler/bootstrap-source-order.txt")
    sys.exit(1)

entries = [
    line.strip().replace("\\", "/")
    for line in bootstrap_order.read_text(encoding="utf-8").splitlines()
    if line.strip() and not line.lstrip().startswith("#")
]
if not entries or entries[-1] != "src/main.rz":
    print("compiler-semantic-modules: FAIL: bootstrap order must end with src/main.rz")
    sys.exit(1)

expected = {path.relative_to(compiler).as_posix() for path in source_root.rglob("*.rz")}
if set(entries) != expected or len(entries) != len(expected):
    print("compiler-semantic-modules: FAIL: bootstrap order must cover compiler/src exactly once")
    sys.exit(1)

def module_namespace(entry: str) -> str:
    stem = Path(entry[4:]).with_suffix("").as_posix().replace("/", "_").replace("-", "_")
    return "raz_compiler_" + stem

for index, entry in enumerate(entries):
    path = compiler / entry
    text = path.read_text(encoding="utf-8")
    namespace = f"namespace {module_namespace(entry)};"
    if namespace not in text:
        print(f"compiler-semantic-modules: FAIL: {entry} is missing explicit namespace: {namespace}")
        sys.exit(1)
    if index == 0:
        continue
    edge = f"public import {module_namespace(entries[index - 1])};"
    if edge not in text:
        print(f"compiler-semantic-modules: FAIL: {entry} is missing semantic predecessor edge: {edge}")
        sys.exit(1)

print(f"compiler-semantic-modules: PASS ({len(entries)} semantic modules; bootstrap order retained only for seed recovery)")
