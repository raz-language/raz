#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Verify that standalone Forge/ObLink sources match Raz's embedded copies.

Raz deliberately keeps Forge and ObLink embedded for an in-tree toolchain build
while publishing the same projects from standalone repositories.  They are one
source contract, not forks: every maintained source file must be byte-identical.

The checker accepts explicit roots for CI, where the three repositories are
checked out as siblings.  In the combined developer workspace it discovers the
usual sibling layout automatically::

    raz-language/
      forge/
      oblink/
      raz/

Only repository/build metadata that is never part of a source checkout is
ignored.  No source-path allowlist exists: a Raz-only modification inside an
embedded component is drift and must be upstreamed to the standalone component
first.
"""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import sys

RAZ_ROOT = Path(__file__).resolve().parents[1]

# These names may exist in a developer checkout but are never maintained source
# content.  Keeping the ignore list name-based also makes it impossible to hide
# one hand-picked Forge or ObLink source file from the equality contract.
IGNORED_ROOT_PARTS = {
    ".git",
    ".idea",
    ".vs",
    ".vscode",
    "build",
    "target",
    "_build",
    "_install",
}
IGNORED_ANY_PARTS = {"__pycache__"}
IGNORED_SUFFIXES = {".pyc", ".pyo"}


def digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def source_files(root: Path) -> dict[str, Path]:
    result: dict[str, Path] = {}
    for path in root.rglob("*"):
        if not path.is_file():
            continue
        relative = path.relative_to(root)
        if relative.parts and relative.parts[0] in IGNORED_ROOT_PARTS:
            continue
        if any(part in IGNORED_ANY_PARTS for part in relative.parts):
            continue
        if path.suffix in IGNORED_SUFFIXES:
            continue
        result[relative.as_posix()] = path
    return result


def compare_component(name: str, standalone: Path, embedded: Path) -> list[str]:
    errors: list[str] = []
    if not standalone.is_dir():
        return [f"{name}: standalone root does not exist: {standalone}"]
    if not embedded.is_dir():
        return [f"{name}: embedded root does not exist: {embedded}"]

    left = source_files(standalone)
    right = source_files(embedded)
    left_names = set(left)
    right_names = set(right)

    for relative in sorted(left_names - right_names):
        errors.append(f"{name}: missing from embedded tree: {relative}")
    for relative in sorted(right_names - left_names):
        errors.append(f"{name}: embedded-only file: {relative}")
    for relative in sorted(left_names & right_names):
        if digest(left[relative]) != digest(right[relative]):
            errors.append(f"{name}: content differs: {relative}")
    return errors


def parse_args() -> argparse.Namespace:
    workspace = RAZ_ROOT.parent
    parser = argparse.ArgumentParser(
        description="Check standalone Forge/ObLink trees against Raz's embedded copies."
    )
    parser.add_argument(
        "--forge-root",
        type=Path,
        default=workspace / "forge",
        help="standalone Forge root (default: ../forge)",
    )
    parser.add_argument(
        "--oblink-root",
        type=Path,
        default=workspace / "oblink",
        help="standalone ObLink root (default: ../oblink)",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    checks = (
        ("Forge", args.forge_root.resolve(), RAZ_ROOT / "src" / "forge"),
        ("ObLink", args.oblink_root.resolve(), RAZ_ROOT / "src" / "oblink"),
    )

    failures: list[str] = []
    for name, standalone, embedded in checks:
        component_failures = compare_component(name, standalone, embedded)
        if component_failures:
            failures.extend(component_failures)
        else:
            count = len(source_files(standalone))
            print(f"component-sync: {name} PASS ({count} files byte-identical)")

    if failures:
        print("component-sync: FAIL", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        print(
            "  update the standalone component and embedded copy together; "
            "Raz must not carry a private Forge/ObLink fork",
            file=sys.stderr,
        )
        return 1

    print("component-sync: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
