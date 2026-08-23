#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Run Raz performance suites against the self-hosted bootstrap compiler."""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
EXE = ".exe" if os.name == "nt" else ""


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bootstrap-profile", choices=("debug", "release"), default="release")
    parser.add_argument("--suite", choices=("smoke", "full"), default="full")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "benchmark-results")
    parser.add_argument("--runtime-samples", type=int, default=5)
    parser.add_argument("--runtime-warmups", type=int, default=1)
    parser.add_argument("--baseline-dir", type=Path)
    args = parser.parse_args()

    compiler = ROOT / "target/bootstrap/repro-1/target" / args.bootstrap_profile / f"raz-compiler{EXE}"
    if not compiler.is_file():
        raise SystemExit(f"performance artifacts: self-hosted compiler not found: {compiler}")
    args.output_dir.mkdir(parents=True, exist_ok=True)
    compiler_json = args.output_dir / "compiler.json"
    runtime_json = args.output_dir / "runtime.json"
    commands = [
        [sys.executable, str(ROOT / "tools/benchmark-compiler.py"), "--raz", str(compiler), "--suite", args.suite, "--output", str(compiler_json)],
        [sys.executable, str(ROOT / "tools/benchmark-runtime.py"), "--raz", str(compiler), "--samples", str(args.runtime_samples), "--warmups", str(args.runtime_warmups), "--output", str(runtime_json)],
    ]
    for command in commands:
        result = subprocess.run(command, cwd=ROOT)
        if result.returncode != 0:
            return result.returncode
    if args.baseline_dir:
        for current in (compiler_json, runtime_json):
            baseline = args.baseline_dir / current.name
            if baseline.is_file():
                result = subprocess.run([sys.executable, str(ROOT / "tools/check-performance-regression.py"), str(current), str(baseline)], cwd=ROOT)
                if result.returncode != 0:
                    return result.returncode
            else:
                print(f"performance baseline: no baseline for {current.name}; measurement retained without enforcement")
    print(f"performance qualification: PASS ({args.output_dir})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
