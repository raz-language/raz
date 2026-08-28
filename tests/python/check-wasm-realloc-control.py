#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "tests/examples/backends/wasm_realloc_control.rz"

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--raz", required=True)
    args = ap.parse_args()
    raz = Path(args.raz).resolve()
    node = shutil.which("node")
    if node is None:
        print("wasm-realloc-control: node is required")
        return 1
    with tempfile.TemporaryDirectory(prefix="raz-wasm-realloc-control-") as td:
        work = Path(td)
        source = work / "main.rz"
        wasm = work / "main.wasm"
        shutil.copy2(SOURCE, source)
        env = os.environ.copy()
        env["RAZ_HOME"] = str(ROOT)
        built = subprocess.run(
            [str(raz), "--backend=wasm", str(source), str(wasm)],
            cwd=work,
            env=env,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        if built.returncode != 0:
            print(built.stdout)
            return 1
        validated = subprocess.run(
            [node, "-e", "const fs=require('fs');const b=fs.readFileSync(process.argv[1]);if(!WebAssembly.validate(b))process.exit(1)", str(wasm)],
            cwd=work,
            env=env,
        )
        if validated.returncode != 0:
            print("wasm-realloc-control: emitted realloc runtime is not engine-valid")
            return 1
    print("wasm-realloc-control: PASS (realloc early-return control flow is engine-valid)")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
