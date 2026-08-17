#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import sys

root = Path(__file__).resolve().parents[2]
liveness = (root / "compiler/src/mir/analysis/liveness.rz").read_text(encoding="utf-8")
dce = (root / "compiler/src/mir/transform/dce.rz").read_text(encoding="utf-8")
builder = (root / "compiler/src/mir/core/builder.rz").read_text(encoding="utf-8")

checks = {
    "MirUseInfo stores counts only": "i64 last_uses;" not in liveness and "i64 use_counts;" in liveness,
    "use-count arena relies on allocator zero fill": "return info.use_counts != 0;" in liveness,
    "DCE packs keep and pinned bits": "i64 state = raz_compiler_rt_arena_create" in dce and "& 1" in dce and "& 2" in dce and "| 2" in dce,
    "DCE no longer allocates separate keep arena": "i64 keep = raz_compiler_rt_arena_create" not in dce,
    "DCE no longer allocates separate pinned arena": "i64 pinned = raz_compiler_rt_arena_create" not in dce,
    "MIR emission uses bounded unchecked lanes": "index < 524288" in builder and "arena_set_unchecked(out.opcodes" in builder,
    "MIR emission retains checked overflow fallback": "arena_set(out.opcodes, index, opcode)" in builder,
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    for name in failed:
        print(f"mir-dce-storage: FAIL: {name}")
    sys.exit(1)

print("mir-dce-storage: PASS")
print("  DCE uses one packed state arena plus one use-count arena and queue")
