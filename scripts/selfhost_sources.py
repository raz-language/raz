#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Frozen bootstrap source ordering for the Raz-written compiler.

Production project builds compile compiler/src as ordinary semantic modules.
This helper exists only for Stage 0 recovery and recursive fixed-point bootstrap
inputs that still require one deterministic seed order.
"""
from __future__ import annotations

from pathlib import Path


def ordered_sources(root: Path) -> list[Path]:
    root = root.resolve()
    order_path = root / "compiler" / "bootstrap-source-order.txt"
    if not order_path.is_file():
        raise RuntimeError(f"missing compiler bootstrap source order: {order_path}")
    entries = [
        line.strip()
        for line in order_path.read_text(encoding="utf-8").splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    ]
    if not entries:
        raise RuntimeError("compiler/bootstrap-source-order.txt is empty")
    if len(entries) != len(set(entries)):
        raise RuntimeError("compiler/bootstrap-source-order.txt contains duplicate entries")
    paths = [root / "compiler" / entry for entry in entries]
    missing = [path for path in paths if not path.is_file()]
    if missing:
        raise RuntimeError("missing compiler source(s): " + ", ".join(str(p) for p in missing))
    return paths


def combined_source(root: Path) -> str:
    return "".join(path.read_text(encoding="utf-8") for path in ordered_sources(root))
