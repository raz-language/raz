#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Differential Raz frontend parity gate.

Runs every single-file `razc-host --check` conformance case registered in
`tests/CMakeLists.txt` through both the native host compiler compiler and a generated
Raz compiler.  Acceptance/rejection must match exactly.  This complements the
source-structure language audit: marker presence is not enough to prove semantic
parity.
"""
from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path


def run(command: list[str], cwd: Path, timeout: float) -> tuple[int, bool, str]:
    try:
        process = subprocess.run(
            command,
            cwd=cwd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=timeout,
        )
        detail = (process.stderr or process.stdout).strip()
        return process.returncode, False, detail
    except subprocess.TimeoutExpired as exc:
        stdout = exc.stdout.decode(errors="replace") if isinstance(exc.stdout, bytes) else (exc.stdout or "")
        stderr = exc.stderr.decode(errors="replace") if isinstance(exc.stderr, bytes) else (exc.stderr or "")
        return 124, True, (stderr or stdout).strip()


def discover_cases(root: Path) -> list[tuple[str, str]]:
    cmake = (root / "tests" / "CMakeLists.txt").read_text(encoding="utf-8")
    pattern = re.compile(
        r"add_test\(NAME\s+([\w-]+)\s+COMMAND\s+razc_host\s+--check\s+"
        r"\$\{PROJECT_SOURCE_DIR\}/([^\s\)]+)\)"
    )
    cases: list[tuple[str, str]] = []
    seen: set[str] = set()
    for test_name, source in pattern.findall(cmake):
        if source in seen:
            continue
        seen.add(source)
        cases.append((test_name, source))
    return cases


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--native", required=True, type=Path, help="razc-host executable")
    parser.add_argument("--compiler", required=True, type=Path, help="generated Raz compiler executable")
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--allow-mismatches", action="store_true")
    args = parser.parse_args()

    root = args.root.resolve()
    native = args.native.resolve()
    compiler = args.compiler.resolve()
    cases = discover_cases(root)
    results = []

    for test_name, relative in cases:
        source = (root / relative).resolve()
        native_rc, native_timeout, native_detail = run(
            [str(native), "--check", str(source)], root, args.timeout
        )
        compiler_rc, compiler_timeout, compiler_detail = run(
            [str(compiler), "--check", str(source)], root, args.timeout
        )
        native_ok = native_rc == 0 and not native_timeout
        compiler_ok = compiler_rc == 0 and not compiler_timeout
        match = native_ok == compiler_ok and not native_timeout and not compiler_timeout
        results.append(
            {
                "test": test_name,
                "source": relative,
                "match": match,
                "native_ok": native_ok,
                "compiler_ok": compiler_ok,
                "native_rc": native_rc,
                "compiler_rc": compiler_rc,
                "native_timeout": native_timeout,
                "compiler_timeout": compiler_timeout,
                "native_detail": native_detail[-1200:],
                "compiler_detail": compiler_detail[-1200:],
            }
        )

    mismatches = [result for result in results if not result["match"]]
    summary = {
        "total": len(results),
        "matches": len(results) - len(mismatches),
        "mismatches": len(mismatches),
        "native_accept": sum(result["native_ok"] for result in results),
        "compiler_accept": sum(result["compiler_ok"] for result in results),
        "compiler_timeouts": sum(result["compiler_timeout"] for result in results),
    }

    print(
        "Raz native/compiler --check parity: "
        f"{summary['matches']}/{summary['total']} matched; "
        f"{summary['mismatches']} mismatch(es); "
        f"{summary['compiler_timeouts']} compiler timeout(s)"
    )
    for result in mismatches:
        print(
            f"MISMATCH {result['source']}: native={result['native_rc']} "
            f"compiler={result['compiler_rc']}"
            + (" [compiler timeout]" if result["compiler_timeout"] else "")
        )

    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(
            json.dumps({"summary": summary, "results": results}, indent=2) + "\n",
            encoding="utf-8",
        )

    if mismatches and not args.allow_mismatches:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
