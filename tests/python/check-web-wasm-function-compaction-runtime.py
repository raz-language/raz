#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Executable proof that unreachable Raz functions do not shape browser Wasm."""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def run(cmd: list[str], cwd: Path, env: dict[str, str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, cwd=cwd, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)


def read_u32(data: bytes, offset: int) -> tuple[int, int]:
    value = 0
    shift = 0
    while True:
        byte = data[offset]
        offset += 1
        value |= (byte & 0x7F) << shift
        if byte < 0x80:
            return value, offset
        shift += 7


def section_vector_count(data: bytes, wanted: int) -> int:
    offset = 8
    while offset < len(data):
        section_id = data[offset]
        offset += 1
        size, offset = read_u32(data, offset)
        end = offset + size
        if section_id == wanted:
            count, _ = read_u32(data, offset)
            return count
        offset = end
    return 0


def build(raz: Path, project: Path, env: dict[str, str], source: str) -> bytes:
    (project / "src" / "main.rz").write_text(source, encoding="utf-8")
    result = run([str(raz), "build", "--release", "--forge-native", "--forge-structured-only"], project, env)
    if result.returncode != 0:
        raise RuntimeError(result.stdout)
    wasm = sorted((project / "dist" / "assets").glob("app.*.wasm"))
    if len(wasm) != 1:
        raise RuntimeError(f"expected one app.*.wasm, found {len(wasm)}")
    return wasm[0].read_bytes()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--raz", required=True)
    ap.add_argument("--work-root", required=True)
    args = ap.parse_args()

    raz = Path(args.raz).resolve()
    work = Path(args.work_root).resolve()
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)

    env = os.environ.copy()
    env["RAZ_HOME"] = str(ROOT)
    runtime_library = ROOT / "build" / "release" / "src" / "runtime" / "libraz_runtime.a"
    if runtime_library.is_file():
        env["RAZ_RUNTIME_LIBRARY"] = str(runtime_library)

    project = work / "web-wasm-function-compaction"
    created = run([str(raz), "new", str(project), "--target", "web"], work, env)
    if created.returncode != 0:
        print(created.stdout)
        return 1

    base = '''import web;

global mut i64 count = 0;

fn increment() {
    count += 1;
    web::set_text_i64("count", count);
}

fn home() -> bool {
    Page page = Page::new("Compaction");
    page.p_id("count", "0");
    page.button_id("inc", "Increment");
    page.on_click("inc", "increment");
    return page.write_route("/");
}

fn main() -> i64 {
    if (!web::prepare_dist()) { return 1; }
    if (!home()) { return 1; }
    return 0;
}
'''
    try:
        baseline = build(raz, project, env, base)
    except RuntimeError as exc:
        print(exc)
        return 1

    dead = []
    for i in range(64):
        dead.append(f"fn unreachable_{i}(i64 value) -> i64 {{ return value + {i}; }}\n")
    try:
        padded = build(raz, project, env, base + "\n" + "".join(dead))
    except RuntimeError as exc:
        print(exc)
        return 1

    # The source program gained 64 HIR functions, but none are browser roots or
    # dependencies. Per-function types, function declarations, and code bodies
    # must therefore remain exactly the same shape in the browser module.
    for section_id, name in ((1, "type"), (3, "function"), (10, "code")):
        before = section_vector_count(baseline, section_id)
        after = section_vector_count(padded, section_id)
        if before != after:
            print(f"web-wasm-function-compaction-runtime: {name} count grew {before} -> {after}")
            return 1

    print(
        "web-wasm-function-compaction-runtime: PASS "
        "(64 unreachable Raz functions add 0 browser Wasm types/functions/bodies)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
