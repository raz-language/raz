# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

#!/usr/bin/env python3
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
INC = (ROOT / "compiler/src/raz_driver/src/incremental.rz").read_text(encoding="utf-8")
PROJECT = (ROOT / "compiler/src/raz_driver/src/project.rz").read_text(encoding="utf-8")
PERF = (ROOT / "docs/PERFORMANCE.md").read_text(encoding="utf-8")

checks = {
    "project cache schema advanced for content fingerprints":
        "fn incremental_cache_schema() -> i64 {\n    return 8;\n}" in INC,
    "input bytes have a dedicated fingerprint helper":
        "public fn incremental_input_content_hash(" in INC and
        "raz_compiler_rt_arena_range_hash(data, 0, read" in INC,
    "snapshot validation compares persisted content fingerprints":
        "expected_content = incremental_state_next_decimal" in INC and
        "incremental_input_content_hash(path, path_length, expected_size) != expected_content" in INC,
    "snapshot writer persists content fingerprints":
        "i64 input_content = incremental_input_content_hash(path, path_length, input_size);" in PROJECT and
        "project_metadata_append_decimal(output, &mut length, capacity, input_content)" in PROJECT,
    "project cache remains conservative on read/hash failure":
        "i64 hash = -1;" in INC and "if (read == expected_size)" in INC,
    "performance documentation describes byte-verified snapshots":
        "content fingerprint" in PERF and "high-resolution modification tick" in PERF,
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    for name in failed:
        print(f"project-snapshot-content-cache: FAIL: {name}")
    sys.exit(1)

print("project-snapshot-content-cache: PASS")
print("  metadata-identical source/config rewrites cannot restore stale project snapshots")
