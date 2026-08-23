#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0
"""Reject printable numeric arrays used as compiler text.

Text constants in the Raz compiler must use `string`.  A tiny set of filename
buffers intentionally remain numeric because the stage1 runtime ABI consumes
an addressable `i64*`; those declarations must carry an explicit
`intentional-byte-array` comment immediately above them.
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "compiler" / "src"
ARRAY = re.compile(r"\bi64\s+\w+\[\d+\]\s*=\s*\[([^\]]+)\]")
violations = []
allowed = 0
for path in sorted(SRC.rglob("*.rz")):
    lines = path.read_text(encoding="utf-8").splitlines()
    for index, line in enumerate(lines):
        match = ARRAY.search(line)
        if not match:
            continue
        try:
            values = [int(piece.strip()) for piece in match.group(1).split(",")]
        except ValueError:
            continue
        if not values or not all(value in (9, 10, 13) or 32 <= value <= 126 for value in values):
            continue
        previous = lines[index - 1] if index > 0 else ""
        if "intentional-byte-array" in previous:
            allowed += 1
            continue
        rendered = "".join(chr(value) for value in values)
        violations.append((path.relative_to(ROOT), index + 1, rendered))

if violations:
    for path, line, rendered in violations:
        print(f"{path}:{line}: printable numeric text array should be a string: {rendered!r}")
    sys.exit(1)
print(f"compiler-text-literals: PASS ({allowed} intentional raw ABI byte arrays)")
