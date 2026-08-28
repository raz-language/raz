#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import re
import sys
sys.dont_write_bytecode = True


root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / "tools"))
from compiler_sources import ordered_sources
try:
    ordered = ordered_sources(root)
except RuntimeError as error:
    print(f"compiler-source-set: FAIL: {error}")
    sys.exit(1)

source_roots = [root / "compiler" / "src"]
discovered = sorted(path.resolve() for source_root in source_roots for path in source_root.rglob("*.rz"))
if set(discovered) != set(path.resolve() for path in ordered):
    expected = {path.resolve() for path in ordered}
    actual = set(discovered)
    print("compiler-source-set: FAIL: semantic compiler discovery does not cover the compiler package graph exactly")
    for path in sorted(actual - expected):
        print(f"  unlisted: {path.relative_to(root)}")
    for path in sorted(expected - actual):
        print(f"  missing: {path.relative_to(root)}")
    sys.exit(1)

relative = [path.relative_to(root / "compiler").as_posix() for path in ordered]
if relative[-1] != "src/main.rz":
    print("compiler-source-set: FAIL: src/main.rz must be the final compiler source")
    sys.exit(1)

# Physical filenames are descriptive. Production compilation uses explicit
# semantic imports instead of physical concatenation or ordering metadata.
for entry in relative:
    name = Path(entry).name
    if re.match(r"^[0-9]+_", name):
        print(f"compiler-source-set: FAIL: compiler module uses numeric ordering prefix: {entry}")
        sys.exit(1)

main_text = ordered[-1].read_text(encoding="utf-8")
main_defs = re.findall(r"(?m)^fn\s+main\s*\(", main_text)
if len(main_defs) != 1:
    print("compiler-source-set: FAIL: src/main.rz must define exactly one main function")
    sys.exit(1)
for path in ordered[:-1]:
    if re.search(r"(?m)^fn\s+main\s*\(", path.read_text(encoding="utf-8")):
        print(f"compiler-source-set: FAIL: non-entry module defines main: {path.relative_to(root)}")
        sys.exit(1)

line_counts = [(path, len(path.read_text(encoding="utf-8").splitlines())) for path in ordered]
if line_counts[-1][1] > 750:
    print(f"compiler-source-set: FAIL: entrypoint regressed into a monolith ({line_counts[-1][1]} lines)")
    sys.exit(1)
largest_path, largest_lines = max(line_counts, key=lambda item: item[1])
if largest_lines > 5000:
    print(f"compiler-source-set: FAIL: compiler module too large: {largest_path.relative_to(root)} ({largest_lines} lines)")
    sys.exit(1)

print(
    f"compiler-source-set: PASS ({len(ordered)} modules, "
    f"main={line_counts[-1][1]} lines, largest={largest_path.name}:{largest_lines})"
)
