#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EXPECTED = "https://raw.githubusercontent.com/raz-language/packages/main"


def fail(message: str) -> None:
    raise SystemExit(f"official-registry: FAIL: {message}")


def main() -> int:
    transport = (ROOT / "compiler/src/driver/registry_transport.rz").read_text(encoding="utf-8")
    registry = (ROOT / "compiler/src/driver/registry.rz").read_text(encoding="utf-8")
    package = (ROOT / "compiler/src/driver/package.rz").read_text(encoding="utf-8")

    match = re.search(
        r"fn registry_default_base\([^)]*\) -> i64 \{\s*"
        r"i64 value\[(\d+)\] = \[([^\]]+)\];",
        transport,
        re.S,
    )
    if not match:
        fail("could not locate registry_default_base byte literal")
    declared = int(match.group(1))
    values = [int(value.strip()) for value in match.group(2).split(",") if value.strip()]
    if declared != len(values):
        fail(f"default registry literal declares {declared} bytes but contains {len(values)}")
    try:
        decoded = bytes(values).decode("ascii")
    except (ValueError, UnicodeDecodeError) as exc:
        fail(f"default registry literal is not ASCII: {exc}")
    if decoded != EXPECTED:
        fail(f"default registry is {decoded!r}, expected {EXPECTED!r}")
    if f"capacity < {declared}" not in transport or f"while (i < {declared})" not in transport or f"return {declared};" not in transport:
        fail("registry_default_base length guards do not match its byte literal")

    if "package_add_official_registry_command" not in registry:
        fail("official registry add resolver is missing")
    if "argc != 3 && argc != 4" not in package:
        fail("raz add does not accept the one-argument official package form")
    if "prepared_submission = true" not in registry or "i64 msg[9] = [80, 114, 101, 112, 97, 114, 101, 100, 32]" not in registry:
        fail("default raz publish is not distinguished from direct private-registry publishing")

    publish_path = bytes([46,114,97,122,45,112,117,98,108,105,115,104,47]).decode("ascii")
    if publish_path != ".raz-publish/":
        fail("internal test error decoding publish path")
    if "i64 publish[13]" not in transport or "registry_path_prefix(path, length, &publish, 13)" not in transport:
        fail(".raz-publish/ is not excluded from deterministic package trees")

    print(f"official-registry: PASS ({EXPECTED}; shorthand add + immutable submission staging)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
