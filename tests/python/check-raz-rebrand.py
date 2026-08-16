#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Reject residual Xyft/Dash branding and obsolete Raz source conventions."""
from __future__ import annotations

from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
SKIP_PARTS = {"build", ".git", ".raz", "target", "__pycache__"}
SEMANTIC_TARGETS = {
    Path("src/forge/include/forge/target"),
    Path("src/forge/src/target"),
    Path("src/forge/tests/target"),
}
OLD_BRAND = "xy" + "ft"
OLD_TEXT = re.compile(r"(?i)" + re.escape(OLD_BRAND))
OLD_PATH = re.compile(r"(?i)" + re.escape(OLD_BRAND))
POLICY_TEXT_EXCEPTIONS = {
    Path("tests/python/check-raz-rebrand.py"),
    Path("tests/python/check-repository-hygiene.py"),
}

BYTE_ARRAY = re.compile(r"(?<!\d)(?:\d{1,3}\s*,\s*){3,}\d{1,3}(?!\d)")
NUMERIC_ARRAY = re.compile(r"i64\s+\w+\[\d+\]\s*=\s*\[([0-9,\s]+)\];")


def semantic_target(rel: Path) -> bool:
    return any(rel == root or root in rel.parents for root in SEMANTIC_TARGETS)


def maintained(path: Path) -> bool:
    rel = path.relative_to(ROOT)
    if any(part in SKIP_PARTS for part in rel.parts) and not semantic_target(rel):
        return False
    return path.is_file()


def decoded_byte_old(text: str) -> bool:
    for match in BYTE_ARRAY.finditer(text):
        values = [int(x) for x in re.findall(r"\d{1,3}", match.group())]
        if not values or not all(0 <= value <= 255 for value in values):
            continue
        decoded = bytes(values).decode("latin1", errors="ignore")
        if OLD_BRAND in decoded.lower() or "." + "xy" in decoded.lower():
            return True
    return False


def decoded_packed_old(text: str) -> bool:
    for match in NUMERIC_ARRAY.finditer(text):
        values = [int(x) for x in re.findall(r"\d+", match.group(1))]
        if not values or max(values) < 256:
            continue
        raw = bytearray()
        for word in values:
            if word < 0 or word > (1 << 63) - 1:
                raw = bytearray()
                break
            for byte_index in range(7):
                raw.append((word >> (byte_index * 8)) & 0xFF)
        decoded = raw.decode("latin1", errors="ignore")
        if OLD_BRAND in decoded.lower() or "." + "xy" in decoded.lower():
            return True
    return False


def charwise_old(text: str) -> bool:
    # Catch generated strings emitted one byte at a time rather than in arrays.
    integers = [int(match.group()) for match in re.finditer(r"(?<![A-Za-z_])\d+(?![A-Za-z_])", text)]
    needles = tuple(tuple(token.encode("ascii")) for token in (OLD_BRAND, OLD_BRAND.capitalize(), OLD_BRAND.upper()))
    for needle in needles:
        width = len(needle)
        if any(integers[index:index + width] == list(needle) for index in range(len(integers) - width + 1)):
            return True
    return False


def main() -> int:
    problems: list[str] = []
    rz_count = 0
    for path in ROOT.rglob("*"):
        rel = path.relative_to(ROOT)
        if any(part in SKIP_PARTS for part in rel.parts) and not semantic_target(rel):
            continue
        if OLD_PATH.search(rel.as_posix()):
            problems.append(f"old-brand path: {rel}")
        if not path.is_file():
            continue
        if path.suffix.lower() == ".xy":
            problems.append(f"obsolete .xy source: {rel}")
        if path.suffix.lower() == ".rz":
            rz_count += 1
        try:
            text = path.read_text(encoding="utf-8")
        except (UnicodeDecodeError, OSError):
            continue
        if rel not in POLICY_TEXT_EXCEPTIONS and OLD_TEXT.search(text):
            problems.append(f"old-brand text: {rel}")
        if rel not in POLICY_TEXT_EXCEPTIONS and decoded_byte_old(text):
            problems.append(f"old-brand encoded byte array: {rel}")
        if rel not in POLICY_TEXT_EXCEPTIONS and decoded_packed_old(text):
            problems.append(f"old-brand packed text: {rel}")
        if path.suffix.lower() == ".rz" and charwise_old(text):
            problems.append(f"old-brand bytewise emitter: {rel}")

    if rz_count == 0:
        problems.append("no .rz source files found")

    if problems:
        print("raz-rebrand: FAIL")
        for problem in sorted(set(problems)):
            print(f"  {problem}")
        return 1

    print(f"raz-rebrand: PASS ({rz_count} .rz source files; no legacy brand/source identity remains)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
