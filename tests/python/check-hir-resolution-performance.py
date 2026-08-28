#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SYMBOLS = (ROOT / "compiler/src/raz_hir/src/hir/core/symbols.rz").read_text(encoding="utf-8")
BUILDER = (ROOT / "compiler/src/raz_hir/src/hir/core/builder.rz").read_text(encoding="utf-8")

checks = {
    "declaration visibility uses binary search": all(
        token in SYMBOLS
        for token in (
            "i64 high = builder.declaration_visibility_count;",
            "i64 middle = low + (high - low) / 2;",
            "declaration_visibility_offsets, middle",
            "declaration_visibility_values, low - 1",
        )
    ),
    "declaration visibility no longer replays the declaration prefix": (
        "while (index < builder.declaration_visibility_count)" not in SYMBOLS
    ),
    "zero-filled import buckets are not redundantly initialized": (
        "while (import_bucket_slot < 8192)" not in BUILDER
    ),
    "zero-filled function buckets are not redundantly initialized": (
        "while (function_lookup_slot < 32768)" not in BUILDER
    ),
    "zero-filled top-level occupancy table is not redundantly initialized": (
        "while (top_level_slot < 32768)" not in BUILDER
    ),
    "symbol cache uses zero-safe encoded identity": (
        "symbol_cache_kinds, slot) == kind + 1" in SYMBOLS
        and "symbol_cache_kinds, slot, kind + 1" in SYMBOLS
    ),
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    print("hir-resolution-performance: FAIL")
    for name in failed:
        print(f"  - {name}")
    raise SystemExit(1)

print(f"hir-resolution-performance: PASS ({len(checks)} contracts)")
