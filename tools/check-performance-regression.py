#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Compare Raz benchmark JSON against an intentional baseline."""
from __future__ import annotations

import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def load(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def compiler_map(payload: dict) -> dict[str, dict]:
    return {row["name"]: row for row in payload.get("results", [])}


def runtime_map(payload: dict) -> dict[str, dict]:
    return {f"{row['workload']}|{row['backend']}|{row['opt']}": row for row in payload.get("results", [])}


def ratio(current: float | int | None, baseline: float | int | None) -> float | None:
    if current is None or baseline in (None, 0):
        return None
    return float(current) / float(baseline)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("current", type=Path)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("--thresholds", type=Path, default=ROOT / "benchmarks/config/performance-thresholds.json")
    args = parser.parse_args()
    current = load(args.current)
    baseline = load(args.baseline)
    thresholds = load(args.thresholds)
    if current.get("kind") != baseline.get("kind"):
        raise SystemExit("performance regression: current/baseline kinds differ")
    failures = []
    if current["kind"] == "compiler":
        cur = compiler_map(current)
        base = compiler_map(baseline)
        limit = float(thresholds["compile_time_max_regression_ratio"])
        rss_limit = float(thresholds["peak_rss_max_regression_ratio"])
        for key in sorted(cur.keys() & base.keys()):
            r = ratio(cur[key].get("wall_seconds"), base[key].get("wall_seconds"))
            if r is not None and r > limit:
                failures.append(f"{key}: compile wall ratio {r:.3f} > {limit:.3f}")
            rr = ratio(cur[key].get("peak_rss_bytes"), base[key].get("peak_rss_bytes"))
            if rr is not None and rr > rss_limit:
                failures.append(f"{key}: peak RSS ratio {rr:.3f} > {rss_limit:.3f}")
    else:
        cur = runtime_map(current)
        base = runtime_map(baseline)
        compile_limit = float(thresholds["compile_time_max_regression_ratio"])
        runtime_limit = float(thresholds["runtime_time_max_regression_ratio"])
        size_limit = float(thresholds["artifact_size_max_regression_ratio"])
        for key in sorted(cur.keys() & base.keys()):
            cr = cur[key]
            br = base[key]
            checks = (
                ("compile wall", ratio(cr.get("compile_wall_seconds"), br.get("compile_wall_seconds")), compile_limit),
                ("runtime median", ratio(cr.get("runtime_wall_seconds", {}).get("median"), br.get("runtime_wall_seconds", {}).get("median")), runtime_limit),
                ("artifact size", ratio(cr.get("artifact_bytes"), br.get("artifact_bytes")), size_limit),
            )
            for label, r, limit in checks:
                if r is not None and r > limit:
                    failures.append(f"{key}: {label} ratio {r:.3f} > {limit:.3f}")
    if failures:
        print("performance regression: FAIL")
        for failure in failures:
            print("  " + failure)
        return 1
    print("performance regression: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
