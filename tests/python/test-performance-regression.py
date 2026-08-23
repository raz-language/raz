#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Self-test the performance regression threshold comparator."""
from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "tools/check-performance-regression.py"


def write(path: Path, wall: float) -> None:
    path.write_text(json.dumps({
        "schema": 1,
        "kind": "compiler",
        "results": [{"name": "check-cold", "wall_seconds": wall, "peak_rss_bytes": 1000}],
    }), encoding="utf-8")


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="raz-perf-regression-") as raw:
        root = Path(raw)
        baseline = root / "baseline.json"
        good = root / "good.json"
        bad = root / "bad.json"
        write(baseline, 1.0)
        write(good, 1.10)
        write(bad, 1.30)
        good_result = subprocess.run([sys.executable, str(TOOL), str(good), str(baseline)], cwd=ROOT, capture_output=True, text=True)
        if good_result.returncode != 0:
            print(good_result.stdout + good_result.stderr)
            return 1
        bad_result = subprocess.run([sys.executable, str(TOOL), str(bad), str(baseline)], cwd=ROOT, capture_output=True, text=True)
        if bad_result.returncode == 0 or "compile wall ratio" not in bad_result.stdout:
            print("performance-regression-selftest: FAIL: deliberate regression was not rejected")
            return 1
    print("performance-regression-selftest: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
