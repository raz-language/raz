#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import hashlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "tests" / "data" / "host-compiler-contract.sha256"


def digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()

expected: dict[str, str] = {}
for raw in MANIFEST.read_text(encoding="utf-8").splitlines():
    line = raw.strip()
    if not line or line.startswith("#"):
        continue
    value, relative = line.split("  ", 1)
    expected[relative] = value

actual_paths: set[str] = set()
for relative in expected:
    path = ROOT / relative
    if path.is_file():
        actual_paths.add(relative)

missing = sorted(set(expected) - actual_paths)
changed = sorted(
    relative for relative in actual_paths if digest(ROOT / relative) != expected[relative]
)

if missing or changed:
    print("host-compiler-contract: FAIL")
    for relative in missing:
        print(f"  missing: {relative}")
    for relative in changed:
        print(f"  changed: {relative}")
    print("  host compiler language semantics are compatibility-pinned. Implement new language semantics in compiler/src/*.rz.")
    print("  Update the compatibility manifest only for an intentional bootstrap-compatibility change.")
    raise SystemExit(1)

print(f"host-compiler-contract: PASS ({len(expected)} host compiler contract files)")
print("  new language semantics belong to the production Raz compiler")
