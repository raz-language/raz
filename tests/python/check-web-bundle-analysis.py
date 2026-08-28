#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0
from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]


def run(cmd: list[str], cwd: Path, env: dict[str, str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, cwd=cwd, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--raz", required=True)
    ap.add_argument("--work-root", required=True)
    args = ap.parse_args()

    work = Path(args.work_root).resolve()
    shutil.rmtree(work, ignore_errors=True)
    shutil.copytree(ROOT / "tests" / "examples" / "web" / "reactive", work)
    env = os.environ.copy()
    env["RAZ_HOME"] = str(ROOT)
    raz = str(Path(args.raz).resolve())

    result = run([raz, "build", "--release", "--analyze"], work, env)
    if result.returncode != 0:
        print(result.stdout)
        return result.returncode

    report_path = work / "target" / "release" / "web-bundle-analysis.txt"
    if not report_path.is_file():
        print("web-bundle-analysis: target/release/web-bundle-analysis.txt was not written")
        return 1
    report = report_path.read_text(encoding="utf-8")
    if "Raz web release bundle analysis" not in report or "Summary:" not in report:
        print("web-bundle-analysis: report header/summary missing")
        return 1
    for category in ("html", "css", "js", "wasm", "meta"):
        if not re.search(rf"^\s+{category}\b.*\b\d+ B$", report, re.MULTILINE):
            print(f"web-bundle-analysis: category {category!r} missing from summary")
            return 1
    if "Logical asset map:" not in report or "assets/app.css" not in report or "assets/app.js" not in report or "assets/app.wasm" not in report:
        print("web-bundle-analysis: logical-to-fingerprinted asset map missing")
        return 1

    dist = work / "dist"
    deployable_total = sum(p.stat().st_size for p in dist.rglob("*") if p.is_file())
    match = re.search(r"^\s+total\s+(\d+) B$", report, re.MULTILINE)
    if not match or int(match.group(1)) != deployable_total:
        got = match.group(1) if match else "missing"
        print(f"web-bundle-analysis: total does not match finalized dist tree ({got} != {deployable_total})")
        return 1
    if (dist / "web-bundle-analysis.txt").exists():
        print("web-bundle-analysis: analysis report leaked into deployable dist tree")
        return 1

    invalid = run([raz, "build", "--analyze"], work, env)
    if invalid.returncode == 0 or "--analyze requires a target=web release build" not in invalid.stdout:
        print("web-bundle-analysis: --analyze without --release was not rejected")
        print(invalid.stdout)
        return 1

    print(f"web-bundle-analysis: PASS ({deployable_total} finalized deployable bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
