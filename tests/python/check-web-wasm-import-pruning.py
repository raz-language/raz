#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]


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


def wasm_imports(path: Path) -> list[tuple[str, str]]:
    data = path.read_bytes()
    if data[:8] != b"\x00asm\x01\x00\x00\x00":
        raise AssertionError("not a WebAssembly 1.0 module")
    offset = 8
    while offset < len(data):
        section_id = data[offset]
        offset += 1
        size, offset = read_u32(data, offset)
        end = offset + size
        if section_id != 2:
            offset = end
            continue
        count, offset = read_u32(data, offset)
        result: list[tuple[str, str]] = []
        for _ in range(count):
            n, offset = read_u32(data, offset)
            module = data[offset : offset + n].decode("utf-8")
            offset += n
            n, offset = read_u32(data, offset)
            name = data[offset : offset + n].decode("utf-8")
            offset += n
            kind = data[offset]
            offset += 1
            if kind == 0:
                _, offset = read_u32(data, offset)
            elif kind == 1:
                offset += 1
                flags, offset = read_u32(data, offset)
                _, offset = read_u32(data, offset)
                if flags & 1:
                    _, offset = read_u32(data, offset)
            elif kind == 2:
                flags, offset = read_u32(data, offset)
                _, offset = read_u32(data, offset)
                if flags & 1:
                    _, offset = read_u32(data, offset)
            elif kind == 3:
                offset += 2
            else:
                raise AssertionError(f"unknown import kind {kind}")
            result.append((module, name))
        return result
    return []


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--raz", required=True)
    ap.add_argument("--work-root", required=True)
    args = ap.parse_args()

    work_root = Path(args.work_root).resolve()
    check = ROOT / "tests" / "python" / "check-web-interactive.py"
    result = subprocess.run(
        [sys.executable, str(check), "--raz", str(Path(args.raz).resolve()), "--work-root", str(work_root)],
        cwd=ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    if result.returncode != 0:
        print(result.stdout)
        return result.returncode

    wasm_files = list((work_root / "web-interactive" / "dist" / "assets").glob("app.*.wasm"))
    if len(wasm_files) != 1:
        print("web-wasm-import-pruning: expected exactly one release WASM module")
        return 1
    imports = wasm_imports(wasm_files[0])
    browser = [name for module, name in imports if module == "raz_web"]
    wasi = [name for module, name in imports if module == "wasi_snapshot_preview1"]

    if browser != ["dom_set_text_i64"]:
        print(f"web-wasm-import-pruning: expected only dom_set_text_i64, got {browser}")
        return 1
    if len(wasi) != 24 or len(imports) != 25:
        print(f"web-wasm-import-pruning: expected 24 WASI + 1 browser import, got {len(wasi)} + {len(browser)}")
        return 1

    print("web-wasm-import-pruning: PASS (49 browser imports pruned to the 1 reachable host call)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
