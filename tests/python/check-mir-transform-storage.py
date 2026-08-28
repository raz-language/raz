#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import sys

root = Path(__file__).resolve().parents[2]
remap = (root / "compiler/src/raz_mir_opt/src/mir_opt/transform/remap.rz").read_text(encoding="utf-8")
live = (root / "compiler/src/raz_mir/src/mir/analysis/liveness.rz").read_text(encoding="utf-8")
dce = (root / "compiler/src/raz_mir_opt/src/mir_opt/transform/dce.rz").read_text(encoding="utf-8")

checks = {
    "instruction map uses one arena": "i64 entries;" in remap and "i64 next_kept;" not in remap and "i64 old_to_new;" not in remap,
    "removed entries encode next kept target": "-(next + 1)" in remap and "return -encoded - 1;" in remap,
    "kept entries use plus-one encoding": "map.kept_count + 1" in remap and "return encoded - 1;" in remap,
    "remap lane uses bounded fast access": "arena_get_unchecked(map.entries" in remap and "arena_set_unchecked(map.entries" in remap,
    "register operands use one opcode mask": "fn mir_register_operand_mask" in live and "register_mask&(1 << operand)" in live,
    "remap reuses operand mask": "register_mask&(1 << operand)" in remap,
    "DCE reuses operand mask": "register_mask&(1 << operand)" in dce,
    "DCE temporary tables use bounded fast access": "arena_get_unchecked(state" in dce and "arena_set_unchecked(queue" in dce and "arena_get_unchecked(uses.use_counts" in dce,
}


def check_signed_remap_model(keep: list[bool]) -> bool:
    kept_count = sum(1 for item in keep if item)
    entries = [0] * (len(keep) + 1)
    compact = 0
    for index, retained in enumerate(keep):
        if retained:
            entries[index] = compact + 1
            compact += 1
    entries[len(keep)] = kept_count + 1
    next_kept = kept_count
    for index in range(len(keep) - 1, -1, -1):
        encoded = entries[index]
        if encoded > 0:
            next_kept = encoded - 1
        else:
            entries[index] = -(next_kept + 1)

    reference_values = {}
    compact = 0
    for index, retained in enumerate(keep):
        if retained:
            reference_values[index] = compact
            compact += 1
    for index, retained in enumerate(keep):
        encoded = entries[index]
        mapped_value = encoded - 1 if encoded > 0 else -1
        expected_value = reference_values.get(index, -1)
        if mapped_value != expected_value:
            return False
        mapped_target = encoded - 1 if encoded > 0 else -encoded - 1
        expected_target = kept_count
        for candidate in range(index, len(keep)):
            if keep[candidate]:
                expected_target = reference_values[candidate]
                break
        if mapped_target != expected_target:
            return False
    return entries[-1] - 1 == kept_count

for pattern in (
    [],
    [True],
    [False],
    [False, True, False, True, False],
    [True, False, False, True],
    [False, False, False],
    [True, True, True],
):
    if not check_signed_remap_model(pattern):
        print(f"mir-transform-storage: FAIL: signed remap model {pattern}")
        sys.exit(1)

failed = [name for name, ok in checks.items() if not ok]
if failed:
    for name in failed:
        print(f"mir-transform-storage: FAIL: {name}")
    sys.exit(1)

print("mir-transform-storage: PASS")
print("  compaction uses one signed remap arena and MIR register-use masks are computed once per instruction")
