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


def browser_abi_names() -> set[str]:
    source = (ROOT / "compiler/src/raz_codegen_wasm/src/wasm/browser_host.rz").read_text(encoding="utf-8")
    import re
    return set(re.findall(r'wasm_browser_emit_import_literal\(section, "([^"]+)"', source))


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
    if wasi or len(imports) != 1:
        print(f"web-wasm-import-pruning: expected 0 WASI + 1 browser import, got {len(wasi)} + {len(browser)}")
        return 1

    # Browser modules are callable libraries, not WASI commands. `_start` and
    # wasi_snapshot_preview1 must both be absent from the binary string table.
    data = wasm_files[0].read_bytes()
    if b"_start" in data or b"wasi_snapshot_preview1" in data:
        print("web-wasm-import-pruning: browser module still carries WASI command surface")
        return 1

    js_files = list((work_root / "web-interactive" / "dist" / "assets").glob("app.*.js"))
    if len(js_files) != 1:
        print("web-wasm-import-pruning: expected exactly one release JS host")
        return 1
    js = js_files[0].read_text(encoding="utf-8")
    present = sorted(name for name in browser_abi_names() if f"{name}:" in js)
    if present != ["dom_set_text_i64"]:
        print(f"web-wasm-import-pruning: JS host did not mirror Wasm imports: {present}")
        return 1
    if "/*raz-web-import:" in js:
        print("web-wasm-import-pruning: private host-pruning marker leaked into dist")
        return 1

    print("web-wasm-import-pruning: PASS (Wasm + JS host pruned from 77 capabilities to dom_set_text_i64 only; WASI/_start removed)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
