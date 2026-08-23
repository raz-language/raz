#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Structural contracts for Raz performance qualification."""
from __future__ import annotations

import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]


def require(condition: bool, message: str) -> None:
    if not condition:
        print(f"phase3-performance: FAIL: {message}")
        raise SystemExit(1)


def main() -> int:
    files = {
        "compiler harness": ROOT / "tools/benchmark-compiler.py",
        "runtime harness": ROOT / "tools/benchmark-runtime.py",
        "regression comparator": ROOT / "tools/check-performance-regression.py",
        "bootstrap benchmark runner": ROOT / "tools/run-benchmarks-from-bootstrap.py",
        "threshold policy": ROOT / "benchmarks/config/performance-thresholds.json",
        "performance docs": ROOT / "docs/PERFORMANCE-QUALIFICATION.md",
    }
    for label, path in files.items():
        require(path.is_file(), f"missing {label}: {path.relative_to(ROOT)}")
    compiler = files["compiler harness"].read_text(encoding="utf-8")
    runtime = files["runtime harness"].read_text(encoding="utf-8")
    regression = files["regression comparator"].read_text(encoding="utf-8")
    lexer = (ROOT / "compiler/src/frontend/lexer.rz").read_text(encoding="utf-8")
    builder = (ROOT / "compiler/src/hir/core/builder.rz").read_text(encoding="utf-8")
    symbols = (ROOT / "compiler/src/hir/core/symbols.rz").read_text(encoding="utf-8")
    ownership = (ROOT / "compiler/src/hir/semantic/ownership.rz").read_text(encoding="utf-8")
    require("check-cold" in compiler and "check-warm" in compiler, "compiler harness must measure cold/warm checks")
    require("check-incremental-leaf-edit" in compiler, "compiler harness must measure incremental edits")
    require("selfhost-check-cold" in compiler, "full compiler project must be benchmarked")
    require("query_profile" in compiler and "peak_rss_bytes" in compiler, "compiler harness must retain query/RSS metrics")
    for backend in ("forge", "llvm"):
        require(backend in runtime, f"runtime harness missing {backend}")
    require('"0,1,2,3,s,z"' in runtime, "runtime harness must cover every supported optimization level")
    require("runtime_internal_nanos" in runtime and "artifact_bytes" in runtime, "runtime harness must retain internal timing and artifact size")
    workloads = tuple((ROOT / "benchmarks/reference/raz").glob("*/raz.toml"))
    require(len(workloads) >= 5, f"expected at least five language runtime workloads, found {len(workloads)}")
    policy = json.loads(files["threshold policy"].read_text(encoding="utf-8"))
    for key in ("compile_time_max_regression_ratio", "runtime_time_max_regression_ratio", "artifact_size_max_regression_ratio", "peak_rss_max_regression_ratio"):
        require(key in policy, f"threshold policy missing {key}")
    require("runtime median" in regression and "compile wall" in regression, "regression comparator must enforce compile/runtime metrics")
    require("raz_rt_move(bytes, (handle as usize) + offset, length);" in lexer and "raz_rt_move((handle as usize) + offset, bytes, length);" in lexer, "compiler ASCII packing must bulk-copy compact source bytes")
    require("return raz_rt_load_u8((value.bytes as usize) + index);" in lexer, "lexer source-byte reads must bypass generic checked arena access")
    require("symbol_cache_kinds = raz_compiler_rt_arena_create(65536)" in builder, "top-level symbol cache must retain production capacity")
    require("function_lookup_bucket_heads = raz_compiler_rt_arena_create(32768)" in builder, "function-name declaration index must be allocated")
    require("hir_function_lookup_head" in symbols and "hir_function_lookup_next_index" in symbols, "function-name declaration index helpers are required")
    find_start = ownership.index("fn hir_find_function(")
    find_end = ownership.index("\nfn ", find_start + 4)
    find_body = ownership[find_start:find_end]
    require("hir_function_lookup_head" in find_body and "hir_function_lookup_next_index" in find_body, "function resolution must walk indexed name candidates")
    require("while (index < builder.module.function_count)" not in find_body and "while (index <builder.module.function_count)" not in find_body, "function resolution must not regress to whole-table scans")
    print(f"phase3-performance: PASS (13 tools/contracts, {len(workloads)} workloads)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
