#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Keep examples and conformance tests named by feature, not development history."""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HISTORICAL = re.compile(r"(?:^|[-_/])phase\d+(?:$|[-_/])", re.IGNORECASE)


def main() -> int:
    problems: list[str] = []

    examples = ROOT / "examples"
    for path in examples.rglob("*"):
        if HISTORICAL.search(path.relative_to(ROOT).as_posix()):
            problems.append(f"historical example path: {path.relative_to(ROOT)}")

    cmake = (ROOT / "tests" / "CMakeLists.txt").read_text(encoding="utf-8")
    for lineno, line in enumerate(cmake.splitlines(), 1):
        if HISTORICAL.search(line):
            problems.append(f"historical conformance name/path: tests/CMakeLists.txt:{lineno}: {line.strip()}")

    # Runtime/native fixtures should describe the behavior they cover, not a past roadmap phase.
    for rel in [Path("tests/native/runtime/runtime_tests.cpp")]:
        text = (ROOT / rel).read_text(encoding="utf-8")
        for lineno, line in enumerate(text.splitlines(), 1):
            if re.search(r"\bphase\d+\b|phase\d+[-_]", line, re.IGNORECASE):
                problems.append(f"historical fixture label: {rel}:{lineno}: {line.strip()}")

    if problems:
        print("test-layout: FAIL")
        for problem in problems:
            print(f"  {problem}")
        return 1

    print("test-layout: PASS (feature-oriented examples and conformance names)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
