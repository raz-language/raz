#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Every `raz help <command>` must print usage rather than fault.

The project-command help used to build its text from large stack arrays, one per
branch, and the resulting frame was big enough that the emitted prologue skipped
the Windows stack guard page. Nothing covered `raz help <command>`, so the fault
went unnoticed; this keeps every command's help reachable.
"""
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

COMMANDS = [
    "build", "check", "run", "test", "bench", "clean", "new", "init",
    "fmt", "lint", "doc", "add", "remove", "update", "lock", "metadata",
    "graph", "registry", "pack", "publish", "keygen", "fetch",
    "emit", "forge", "llvm", "backends", "targets", "doctor", "version",
]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--raz", required=True, type=Path)
    arguments = parser.parse_args()

    failures: list[str] = []
    for command in COMMANDS:
        result = subprocess.run(
            [str(arguments.raz), "help", command], capture_output=True, text=True, timeout=60
        )
        if result.returncode != 0:
            failures.append(f"raz help {command}: exit {result.returncode}")
        elif not result.stdout.strip():
            failures.append(f"raz help {command}: printed nothing")

    if failures:
        print("cli-command-help: FAIL")
        for failure in failures:
            print(f"  {failure}")
        return 1

    print(f"cli-command-help: PASS ({len(COMMANDS)} commands)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
