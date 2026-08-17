#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Profile the canonical Raz compiler build without modifying the compiler."""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import time


def _link_or_copy(source: str, destination: str) -> str:
    try:
        os.link(source, destination)
        return destination
    except OSError:
        return shutil.copy2(source, destination)


def _stage_compiler(source_root: Path, work: Path) -> None:
    shutil.rmtree(work, ignore_errors=True)
    shutil.copytree(
        source_root,
        work,
        ignore=shutil.ignore_patterns(".raz", "target", "compiler-diagnostic.txt", "forge-phase-profile.txt"),
        copy_function=_link_or_copy,
    )
    order = source_root / "host-source-order.txt"
    manifest = source_root / "raz.toml"
    if not order.is_file() or not manifest.is_file():
        raise RuntimeError(f"{source_root} is not a canonical Raz compiler project")
    (work / "source-order.txt").write_text(order.read_text(encoding="utf-8"), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("compiler", help="production raz-compiler executable to measure")
    parser.add_argument("source_root", help="compiler project root (normally ./compiler)")
    parser.add_argument("workdir", help="disposable profiling workspace")
    parser.add_argument("--output", default="stage-profile.o")
    parser.add_argument("--opt", choices=("0", "1", "2", "3", "s", "z"), default="2")
    parser.add_argument("--status-interval", type=float, default=15.0)
    args = parser.parse_args()

    compiler = Path(args.compiler).resolve()
    source_root = Path(args.source_root).resolve()
    work = Path(args.workdir).resolve()
    if not compiler.is_file():
        raise RuntimeError(f"compiler does not exist: {compiler}")

    _stage_compiler(source_root, work)
    diagnostic = work / "compiler-diagnostic.txt"
    forge_profile = work / "forge-phase-profile.txt"
    query_profile = work / "compiler-query-profile.txt"
    env = os.environ.copy()
    env["RAZ_FORGE_PHASE_PROFILE"] = str(forge_profile)

    command = [
        str(compiler),
        "build",
        "--backend=forge",
        "--forge-native",
        "--forge-structured-only",
        "--profile-queries",
        f"--opt={args.opt}",
        "raz.toml",
        args.output,
    ]
    print("[profile] " + " ".join(command), flush=True)
    start = time.perf_counter()
    process = subprocess.Popen(command, cwd=work, env=env)
    last_diagnostic = ""
    last_report = 0.0
    phase_times: list[tuple[str, float]] = []

    while process.poll() is None:
        elapsed = time.perf_counter() - start
        try:
            raw = diagnostic.read_text(encoding="utf-8").strip()
        except OSError:
            raw = ""
        if raw and raw != last_diagnostic:
            last_diagnostic = raw
            phase_times.append((raw, elapsed))
            print(f"[phase {elapsed:8.3f}s] {raw}", flush=True)
        if args.status_interval > 0 and elapsed - last_report >= args.status_interval:
            last_report = elapsed
            print(f"[profile {elapsed:8.1f}s] compiling", flush=True)
        time.sleep(0.05)

    return_code = process.wait()
    total = time.perf_counter() - start
    print(f"[profile] exit={return_code} total={total:.3f}s")

    previous = 0.0
    for diagnostic_text, elapsed in phase_times:
        print(f"  +{elapsed - previous:8.3f}s  {diagnostic_text}")
        previous = elapsed

    if query_profile.is_file():
        print("\n[semantic-query-profile]")
        print(query_profile.read_text(encoding="utf-8"), end="")
    if forge_profile.is_file():
        print("\n[forge-phase-profile]")
        print(forge_profile.read_text(encoding="utf-8"), end="")
    return return_code


if __name__ == "__main__":
    raise SystemExit(main())
