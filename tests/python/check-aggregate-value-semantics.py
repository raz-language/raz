#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LOWERING = ROOT / "compiler" / "src" / "mir" / "lowering.rz"
text = LOWERING.read_text(encoding="utf-8")

needle = "storage = lower_hir_copy_structure(hir, out, structure, source);"
if needle not in text:
    raise SystemExit("aggregate value-semantics gate: aggregate local lowering does not materialize independent storage")

bad = "storage = lower_hir_expression(hir, out, initializer);"
segment_start = text.find("} else if (kind == 10) {")
segment_end = text.find("} else if (kind == 8) {", segment_start)
if segment_start < 0 or segment_end < 0:
    raise SystemExit("aggregate value-semantics gate: aggregate-local lowering block not found")
segment = text[segment_start:segment_end]
if bad in segment:
    raise SystemExit("aggregate value-semantics gate: aggregate local still aliases initializer storage")

print("aggregate-value-semantics: PASS")
