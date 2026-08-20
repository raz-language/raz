#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Synchronize standalone Forge/ObLink checkouts into Raz.

The standalone repositories are the editing/publication roots.  Raz embeds exact
copies so a source checkout can build one self-contained native toolchain.  This
script deliberately replaces each embedded tree instead of merging it: stale
Raz-only files are drift just as much as changed files are.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import shutil
import subprocess
import sys

RAZ_ROOT = Path(__file__).resolve().parents[1]
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


def source_files(root: Path) -> list[Path]:
    files: list[Path] = []
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
        files.append(relative)
    return files


def replace_component(name: str, standalone: Path, embedded: Path) -> None:
    standalone = standalone.resolve()
    embedded = embedded.resolve()

    if not standalone.is_dir():
        raise SystemExit(f"{name}: standalone root does not exist: {standalone}")
    if standalone == embedded:
        raise SystemExit(f"{name}: refusing to synchronize a component onto itself")
    if not (standalone / "CMakeLists.txt").is_file():
        raise SystemExit(f"{name}: root does not look like a source checkout: {standalone}")

    if embedded.exists():
        shutil.rmtree(embedded)
    embedded.mkdir(parents=True)
    copied = 0
    for relative in source_files(standalone):
        destination = embedded / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(standalone / relative, destination)
        copied += 1
    print(f"component-sync: copied {name} ({copied} files): {standalone} -> {embedded}")


def parse_args() -> argparse.Namespace:
    workspace = RAZ_ROOT.parent
    parser = argparse.ArgumentParser(
        description="Replace Raz's embedded Forge/ObLink trees with sibling standalone sources."
    )
    parser.add_argument("--forge-root", type=Path, default=workspace / "forge")
    parser.add_argument("--oblink-root", type=Path, default=workspace / "oblink")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    replace_component("Forge", args.forge_root, RAZ_ROOT / "src" / "forge")
    replace_component("ObLink", args.oblink_root, RAZ_ROOT / "src" / "oblink")

    checker = RAZ_ROOT / "tools" / "check-embedded-components.py"
    return subprocess.call(
        [
            sys.executable,
            str(checker),
            "--forge-root",
            str(args.forge_root),
            "--oblink-root",
            str(args.oblink_root),
        ]
    )


if __name__ == "__main__":
    raise SystemExit(main())
