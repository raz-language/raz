#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import sys

root = Path(__file__).resolve().parents[2]
inc = (root / "compiler/src/raz_driver/src/incremental.rz").read_text()
required = [
    "/mir.units", "RAZMIRUNITS 2\\n", "fn incremental_persist_mir_units(",
    "mir.function_count", "mir.instruction_count", "mir.function_module_ids",
    "mir.call_arguments", "mir.opcodes", "mir.op_types", "mir.op_a", "mir.op_b", "mir.op_c", "mir.op_d",
]
missing = [x for x in required if x not in inc]
if missing:
    print("mir-unit-cache: FAIL: missing source contracts: " + ", ".join(missing))
    sys.exit(1)

cache = root / "compiler/target/cache/mir.units"
if cache.exists():
    lines = cache.read_text(errors="strict").splitlines()
    if len(lines) < 2 or lines[0] != "RAZMIRUNITS 2" or not lines[1].startswith("S "):
        print("mir-unit-cache: FAIL: malformed live cache header")
        sys.exit(1)
    fields = lines[1].split()
    if len(fields) != 6:
        print("mir-unit-cache: FAIL: malformed summary")
        sys.exit(1)
    functions, instructions, globals_, args = map(int, fields[2:])
    counts = {k: 0 for k in "MFGIA"}
    for line in lines[2:]:
        if len(line) >= 2 and line[1] == " " and line[0] in counts:
            counts[line[0]] += 1
    if counts["F"] != functions or counts["I"] != instructions or counts["G"] != globals_ or counts["A"] != args:
        print(f"mir-unit-cache: FAIL: summary mismatch {counts} vs F={functions} I={instructions} G={globals_} A={args}")
        sys.exit(1)
    if counts["M"] <= 0:
        print("mir-unit-cache: FAIL: no module records")
        sys.exit(1)
    print(f"mir-unit-cache: PASS ({counts['M']} modules, {functions} functions, {instructions} instructions)")
else:
    print("mir-unit-cache: PASS (source contracts; live cache not seeded)")
