#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Benchmark Raz runtime workloads across Forge/LLVM optimization levels."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import platform
import shutil
import statistics
import sys
import tempfile
import time

from perf_common import run_measured, summarize, write_json

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_WORKLOADS = (
    ROOT / "benchmarks/reference/raz/crc32",
    ROOT / "benchmarks/reference/raz/http_parse",
    ROOT / "benchmarks/reference/raz/integer_reduce",
    ROOT / "benchmarks/reference/raz/branch_mix",
    ROOT / "benchmarks/reference/raz/format_i64",
)


def artifact_path(project: Path, backend: str, profile: str) -> Path:
    manifest = project / "raz.toml"
    name = project.name
    try:
        text = manifest.read_text(encoding="utf-8")
        for line in text.splitlines():
            stripped = line.strip()
            if stripped.startswith("name") and "=" in stripped:
                value = stripped.split("=", 1)[1].strip().strip('"')
                if value:
                    name = value
                    break
    except OSError:
        pass
    suffix = ".exe" if os.name == "nt" else ""
    return project / "target" / profile / f"{name}{suffix}"


def run_workload(raz: Path, source: Path, backend: str, opt: str, samples: int, warmups: int) -> dict:
    with tempfile.TemporaryDirectory(prefix="raz-runtime-bench-") as raw:
        project = Path(raw) / source.name
        shutil.copytree(source, project)
        profile = "release"
        build = run_measured(
            [str(raz), "build", f"--backend={backend}", f"--opt={opt}", "--release"],
            cwd=project,
        )
        if build["returncode"] != 0:
            raise RuntimeError(f"{source.name}/{backend}/O{opt} build failed:\n{build['stderr']}")
        binary = artifact_path(project, backend, profile)
        if not binary.is_file():
            raise RuntimeError(f"benchmark artifact missing: {binary}")
        for _ in range(warmups):
            result = run_measured([str(binary)], cwd=project)
            if result["returncode"] != 0:
                raise RuntimeError(f"warmup failed for {source.name}/{backend}/O{opt}: {result['stderr']}")
        observed = []
        internal_nanos = []
        peak_rss = []
        stdout_reference = None
        for _ in range(samples):
            result = run_measured([str(binary)], cwd=project)
            if result["returncode"] != 0:
                raise RuntimeError(f"run failed for {source.name}/{backend}/O{opt}: {result['stderr']}")
            observed.append(result["wall_seconds"])
            if result["peak_rss_bytes"]:
                peak_rss.append(float(result["peak_rss_bytes"]))
            if stdout_reference is None:
                stdout_reference = result["stdout"]
            fields = result["stdout"].strip().split()
            if fields:
                try:
                    internal_nanos.append(float(fields[0]))
                except ValueError:
                    pass
        return {
            "workload": source.name,
            "backend": backend,
            "opt": opt,
            "compile_wall_seconds": build["wall_seconds"],
            "compile_peak_rss_bytes": build["peak_rss_bytes"],
            "artifact_bytes": binary.stat().st_size,
            "runtime_wall_seconds": summarize(observed),
            "runtime_internal_nanos": summarize(internal_nanos) if internal_nanos else None,
            "runtime_peak_rss_bytes": summarize(peak_rss) if peak_rss else None,
            "sample_count": samples,
            "stdout_sample": stdout_reference,
        }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--raz", required=True, type=Path)
    parser.add_argument("--output", type=Path, default=ROOT / "benchmark-runtime-current.json")
    parser.add_argument("--samples", type=int, default=5)
    parser.add_argument("--warmups", type=int, default=1)
    parser.add_argument("--backends", default="forge,llvm")
    parser.add_argument("--opts", default="0,1,2,3,s,z")
    parser.add_argument("--workload", action="append", type=Path)
    args = parser.parse_args()

    workloads = tuple(args.workload) if args.workload else DEFAULT_WORKLOADS
    backends = [item.strip() for item in args.backends.split(",") if item.strip()]
    opts = [item.strip() for item in args.opts.split(",") if item.strip()]
    rows = []
    for workload in workloads:
        path = workload if workload.is_absolute() else ROOT / workload
        for backend in backends:
            for opt in opts:
                print(f"[runtime] {path.name} backend={backend} opt={opt}", flush=True)
                rows.append(run_workload(args.raz.resolve(), path.resolve(), backend, opt, args.samples, args.warmups))

    payload = {
        "schema": 1,
        "kind": "runtime",
        "platform": platform.platform(),
        "python": sys.version.split()[0],
        "timestamp_unix": int(time.time()),
        "results": rows,
    }
    write_json(args.output, payload)
    print(f"runtime benchmark: wrote {args.output} ({len(rows)} rows)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
