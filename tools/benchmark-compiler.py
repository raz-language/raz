#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Measure cold, warm, incremental, and full-project Raz compile performance."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import platform
import shutil
import sys
import tempfile
import time

from perf_common import run_measured, write_json

ROOT = Path(__file__).resolve().parents[1]


def clean_project(project: Path) -> None:
    for name in ("target", ".raz", "build"):
        shutil.rmtree(project / name, ignore_errors=True)


def read_query_profile(cwd: Path) -> dict[str, dict[str, int]]:
    path = cwd / "target" / "profile" / "compiler-query-profile.txt"
    if not path.is_file():
        return {}
    rows: dict[str, dict[str, int]] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        fields = line.split()
        if len(fields) != 3:
            continue
        try:
            rows[fields[0]] = {"hits": int(fields[1]), "misses": int(fields[2])}
        except ValueError:
            continue
    return rows


def measure(label: str, command: list[str], cwd: Path, *, require_zero: bool = True, query_profile: bool = False) -> dict:
    print(f"[compiler] {label}", flush=True)
    result = run_measured(command, cwd=cwd)
    if require_zero and result["returncode"] != 0:
        raise RuntimeError(f"{label} failed:\n{result['stderr']}")
    row = {
        "name": label,
        "wall_seconds": result["wall_seconds"],
        "peak_rss_bytes": result["peak_rss_bytes"],
        "returncode": result["returncode"],
    }
    if query_profile:
        row["query_profile"] = read_query_profile(cwd)
    return row


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--raz", required=True, type=Path)
    parser.add_argument("--output", type=Path, default=ROOT / "benchmark-compiler-current.json")
    parser.add_argument("--project", type=Path, default=ROOT / "benchmarks/reference/raz/http_parse")
    parser.add_argument("--compiler-project", type=Path, default=ROOT / "compiler")
    parser.add_argument("--suite", choices=("smoke", "full"), default="smoke")
    args = parser.parse_args()

    raz = args.raz.resolve()
    rows = []
    with tempfile.TemporaryDirectory(prefix="raz-compiler-bench-") as raw:
        work = Path(raw) / "project"
        shutil.copytree(args.project.resolve(), work)
        clean_project(work)
        rows.append(measure("check-cold", [str(raz), "check", "--profile-queries"], work, query_profile=True))
        rows.append(measure("check-warm", [str(raz), "check", "--profile-queries"], work, query_profile=True))
        main_source = work / "src/main.rz"
        if main_source.is_file():
            original = main_source.read_text(encoding="utf-8")
            main_source.write_text(original + "\n// performance incremental probe\n", encoding="utf-8")
            rows.append(measure("check-incremental-leaf-edit", [str(raz), "check", "--profile-queries"], work, query_profile=True))
            main_source.write_text(original, encoding="utf-8")
        clean_project(work)
        rows.append(measure("forge-build-o0-cold", [str(raz), "build", "--backend=forge", "--opt=0"], work))
        rows.append(measure("forge-build-o0-warm", [str(raz), "build", "--backend=forge", "--opt=0"], work))
        rows.append(measure("llvm-build-o0-warm", [str(raz), "build", "--backend=llvm", "--opt=0"], work))

    if args.suite == "full":
        with tempfile.TemporaryDirectory(prefix="raz-selfhost-bench-") as raw:
            compiler_work = Path(raw) / "compiler"
            shutil.copytree(args.compiler_project.resolve(), compiler_work, ignore=shutil.ignore_patterns("target", ".raz", "build"))
            clean_project(compiler_work)
            rows.append(measure("selfhost-check-cold", [str(raz), "check", "--profile-queries", "raz.toml"], compiler_work, query_profile=True))
            rows.append(measure("selfhost-check-warm", [str(raz), "check", "--profile-queries", "raz.toml"], compiler_work, query_profile=True))

    payload = {
        "schema": 1,
        "kind": "compiler",
        "platform": platform.platform(),
        "python": sys.version.split()[0],
        "timestamp_unix": int(time.time()),
        "suite": args.suite,
        "results": rows,
    }
    write_json(args.output, payload)
    print(f"compiler benchmark: wrote {args.output} ({len(rows)} rows)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
