#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Keep frozen Stage-0's local web driver surface synchronized with compiler_main."""

from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
BOOTSTRAP = (ROOT / "tools" / "bootstrap.py").read_text(encoding="utf-8")
COMPILER_MAIN = (ROOT / "compiler" / "src" / "raz_driver" / "src" / "driver" / "compiler_main.rz").read_text(encoding="utf-8")

# These are external web-codegen/runtime helpers called directly by compiler_main.
# Frozen Stage-0 cannot reliably see them through the modern package re-export graph,
# so tools/bootstrap.py must inject local signature-compatible stubs into the disposable
# seed view. Derive the required set from the real driver so new direct calls fail this
# gate immediately instead of failing a Windows bootstrap several minutes later.
called = set(re.findall(r"\b((?:emit_web_[A-Za-z0-9_]+)|web_runtime_capabilities)\s*\(", COMPILER_MAIN))
assert called, "no direct web driver calls discovered"

missing = []
for name in sorted(called):
    if f'fn {name}(' not in BOOTSTRAP:
        missing.append(name)

if missing:
    for name in missing:
        print(f"stage0-web-driver-stubs: FAIL: missing local seed stub for {name}")
    raise SystemExit(1)

# runtime analysis is also part of the disposable raz_codegen_web package stub so
# any Stage-0 source that resolves it through the package facade remains compatible.
assert 'public fn web_runtime_capabilities(Source& source, HirModule& hir, MirModule& mir) -> i64' in BOOTSTRAP
assert 'fn web_runtime_capabilities(Source& source, HirModule& hir, MirModule& mir) -> i64' in BOOTSTRAP

print(
    "stage0-web-driver-stubs: PASS (" + str(len(called))
    + " direct compiler_main web helpers synchronized: " + ", ".join(sorted(called)) + ")"
)
