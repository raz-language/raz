#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import importlib.util
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
FORMATTER = ROOT / "scripts" / "format-raz.py"
spec = importlib.util.spec_from_file_location("raz_formatter", FORMATTER)
if spec is None or spec.loader is None:
    raise SystemExit("formatter-layout: FAIL unable to load formatter")
module = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = module
spec.loader.exec_module(module)

SOURCE = """// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

fn short(Source& source) -> i64 {
    return tiny(source);
}

fn process(i64 session, Source& source, HirModule& hir, ForgeWriter& mut name, ForgeWriter& mut field_name, ForgeWriter& mut aggregate_name) -> bool {
    i64 structure = 0;
    while (structure < hir.struct_count) {
        if (raz_rt_stage1_arena_get(hir.struct_field_references, structure) != 0 || raz_rt_stage1_arena_get(hir.struct_field_function_types, structure) >= 0) {
            i64 handle = raz_compiler_forge_session_add_aggregate_array_i64(session, aggregate_name.data, aggregate_name.length, 1, name.data, name.length, structure, 0);
            writer_structure_name(name, source, hir, structure);
        }
        structure += 1;
    }
    return true;
}
"""

formatted = module.format_text(SOURCE)
second = module.format_text(formatted)
if formatted != second:
    raise SystemExit("formatter-layout: FAIL formatter is not idempotent")
checks = {
    "mutable reference": "ForgeWriter&mut name,",
    "multiline signature": "fn process(\n    i64 session,",
    "trailing parameter comma": "    ForgeWriter&mut aggregate_name,\n) -> bool {",
    "short call compact": "writer_structure_name(name, source, hir, structure);",
    "boolean wrap": "if (\n            raz_rt_stage1_arena_get(hir.struct_field_references, structure) != 0 ||",
    "long call wrap": "raz_compiler_forge_session_add_aggregate_array_i64(\n",
    "trailing argument comma": "                0,\n            );",
}
for label, needle in checks.items():
    if needle not in formatted:
        print(formatted)
        raise SystemExit(f"formatter-layout: FAIL missing {label}")
for line in formatted.splitlines():
    if len(line) > module.FORMAT_WIDTH and "http" not in line:
        raise SystemExit(f"formatter-layout: FAIL line exceeds width ({len(line)}): {line}")
print("formatter-layout: PASS")
