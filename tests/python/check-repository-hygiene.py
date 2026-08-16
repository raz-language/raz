#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Reject generated/stale artifacts that do not belong in the Raz source tree."""
from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FORBIDDEN_ROOT_FILES = {
    "compiler-all.rz",
    "forge-structured-failure.txt",
    "compiler-diagnostic.txt",
}
FORBIDDEN_SUFFIXES = {".inc", ".xy", ".xyft", ".raz"}
FORBIDDEN_DIR_NAMES = {"build", ".raz", "__pycache__"}
SEMANTIC_TARGETS = {
    Path("src/forge/include/forge/target"),
    Path("src/forge/src/target"),
    Path("src/forge/tests/target"),
}


def semantic_target(path: Path) -> bool:
    rel = path.relative_to(ROOT)
    return any(rel == root or root in rel.parents for root in SEMANTIC_TARGETS)


def main() -> int:
    problems: list[str] = []
    for name in sorted(FORBIDDEN_ROOT_FILES):
        if (ROOT / name).exists():
            problems.append(f"stale/generated root artifact: {name}")

    generated_pdf = ROOT / "docs" / "GETTING-STARTED.pdf"
    if generated_pdf.exists():
        problems.append("generated documentation binary: docs/GETTING-STARTED.pdf")

    for path in ROOT.rglob("*"):
        rel = path.relative_to(ROOT)
        # Build/test/package caches may legitimately exist in a developer checkout.
        # They are ignored here and independently excluded/verified by package-source.
        generated = (
            any(part in {"build", ".raz", "__pycache__"} for part in rel.parts)
            or ("target" in rel.parts and not semantic_target(path))
        )
        if generated:
            continue
        if path.is_file() and path.suffix.lower() in FORBIDDEN_SUFFIXES:
            problems.append(f"forbidden source/artifact extension: {rel}")

    required = ["LICENSE", "NOTICE", ".gitignore"]
    for name in required:
        if not (ROOT / name).is_file():
            problems.append(f"missing repository file: {name}")

    if problems:
        print("repository-hygiene: FAIL")
        for problem in sorted(set(problems)):
            print(f"  {problem}")
        return 1

    print("repository-hygiene: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
