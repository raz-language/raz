# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Deterministic host-compiler source ordering for the Raz production compiler.

The production compiler uses normal module discovery. This helper exposes the
canonical ordering metadata required only by the native host compiler and
compiler-reproducibility qualification.
"""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
COMPILER_ROOT = ROOT / "compiler"
ORDER_FILE = COMPILER_ROOT / "host-source-order.txt"


def ordered_sources(root: Path | None = None) -> list[Path]:
    compiler_root = (root / "compiler") if root is not None else COMPILER_ROOT
    order_file = compiler_root / "host-source-order.txt"
    entries: list[Path] = []
    for raw in order_file.read_text(encoding="utf-8").splitlines():
        item = raw.strip().replace("\\", "/")
        if not item or item.startswith("#"):
            continue
        entries.append(compiler_root / item)
    return entries


def combined_source(root: Path | None = None) -> str:
    return "\n".join(path.read_text(encoding="utf-8") for path in ordered_sources(root))
