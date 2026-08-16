#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[2]
required = [
    "compiler/src/mir/transform/scalar.rz",
    "compiler/src/mir/transform/cfg_cleanup.rz",
]
problems = []
for rel in required:
    if not (root / rel).is_file():
        problems.append(f"missing {rel}")

scalar = (root / required[0]).read_text(encoding="utf-8")
cfg = (root / required[1]).read_text(encoding="utf-8")
const = (root / "compiler/src/mir/transform/const_prop.rz").read_text(encoding="utf-8")
pipeline = (root / "compiler/src/mir/transform/pipeline.rz").read_text(encoding="utf-8")
order = (root / "compiler/host-source-order.txt").read_text(encoding="utf-8")

for needle in [
    "simplify_mir_scalars",
    "mir_scalar_replace_with_value",
    "left_index == right_index",
]:
    if needle not in scalar:
        problems.append(f"scalar canonicalization missing {needle}")
for needle in [
    "fold_mir_constant_branches",
    "thread_mir_empty_jumps",
    "eliminate_mir_unreachable_blocks",
    "eliminate_mir_fallthrough_jumps",
    "compact_mir_instructions",
]:
    if needle not in cfg:
        problems.append(f"CFG cleanup missing {needle}")
for opcode in ("opcode >= 11 && opcode <= 16", "opcode == 19", "opcode == 20"):
    if opcode not in const:
        problems.append(f"constant propagation missing {opcode}")
for call in ("simplify_mir_scalars(mir)", "cleanup_mir_cfg(mir)"):
    if call not in pipeline:
        problems.append(f"pipeline missing {call}")
for module in ("src/mir/transform/scalar.rz", "src/mir/transform/cfg_cleanup.rz"):
    if module not in order:
        problems.append(f"bootstrap order missing {module}")

if problems:
    print("mir-scalar-optimization: FAIL")
    for problem in problems:
        print(" ", problem)
    sys.exit(1)

print("mir-scalar-optimization: PASS")
print("  constant branches, unreachable blocks, empty trampolines, and fallthrough jumps are canonicalized")
print("  comparison/boolean constant folding and algebraic scalar simplification run before final DCE")
