#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Verify the production compiler is a semantic module graph with no order files."""
from pathlib import Path
import re
import sys
sys.dont_write_bytecode = True

root = Path(__file__).resolve().parents[2]
compiler = root / "compiler"
source_root = compiler / "src"
bootstrap_driver = (root / "tools" / "bootstrap.py").read_text(encoding="utf-8")

for forbidden in (compiler / "source-order.txt", compiler / "host-source-order.txt"):
    if forbidden.exists():
        print(f"compiler-semantic-modules: FAIL: ordering metadata must not exist: {forbidden.relative_to(root)}")
        sys.exit(1)

if "def compiler_modules()" not in bootstrap_driver:
    print("compiler-semantic-modules: FAIL: bootstrap has no semantic source discovery")
    sys.exit(1)
for forbidden_text in (
    'line.startswith("namespace raz_compiler_")',
    'line.startswith("public import raz_compiler_")',
    'line.startswith("import raz_compiler_")',
):
    if forbidden_text in bootstrap_driver:
        print(f"compiler-semantic-modules: FAIL: bootstrap contains legacy module handling: {forbidden_text}")
        sys.exit(1)

sources = sorted(source_root.rglob("*.rz"))
if not sources:
    print("compiler-semantic-modules: FAIL: no compiler modules discovered")
    sys.exit(1)
entry = source_root / "main.rz"
if entry not in sources:
    print("compiler-semantic-modules: FAIL: compiler/src/main.rz is missing")
    sys.exit(1)

namespace_re = re.compile(r"(?m)^\s*namespace\s+([A-Za-z_][A-Za-z0-9_]*(?:::[A-Za-z_][A-Za-z0-9_]*)*)\s*;")
import_re = re.compile(r"(?m)^\s*(?:public\s+)?import\s+([A-Za-z_][A-Za-z0-9_]*(?:::[A-Za-z_][A-Za-z0-9_]*)*)\s*;")
namespaces: dict[str, Path] = {}
imports_by_path: dict[Path, set[str]] = {}
for path in sources:
    text = path.read_text(encoding="utf-8")
    match = namespace_re.search(text)
    if match is None:
        print(f"compiler-semantic-modules: FAIL: missing namespace: {path.relative_to(compiler)}")
        sys.exit(1)
    namespace = match.group(1)
    if namespace in namespaces:
        print(f"compiler-semantic-modules: FAIL: duplicate namespace: {namespace}")
        sys.exit(1)
    namespaces[namespace] = path
    imports_by_path[path] = set(import_re.findall(text))

for path, imports in imports_by_path.items():
    for dependency in imports:
        if dependency.startswith("raz_compiler_") and dependency not in namespaces:
            print(
                "compiler-semantic-modules: FAIL: unresolved same-package compiler import "
                f"{dependency} in {path.relative_to(compiler)}"
            )
            sys.exit(1)

print(f"compiler-semantic-modules: PASS ({len(sources)} semantic modules; no source-order metadata)")
