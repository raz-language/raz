#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Executable regression for compiler arena lifetime/width/native-boundary invariants."""
from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def fail(message: str) -> None:
    raise SystemExit(f"compiler-arena-abi: FAIL {message}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--raz", required=True, type=Path)
    parser.add_argument("--work-root", type=Path, default=ROOT / "target" / "compiler-arena-abi")
    args = parser.parse_args()

    raz = args.raz.resolve()
    if not raz.is_file():
        fail(f"compiler missing: {raz}")

    work = args.work_root.resolve()
    shutil.rmtree(work, ignore_errors=True)
    (work / "src").mkdir(parents=True)
    lexer = (ROOT / "compiler" / "src" / "raz_lexer").resolve().as_posix()
    (work / "raz.toml").write_text(
        "\n".join([
            "[package]",
            'name = "compiler-arena-abi"',
            'version = "1.0.0"',
            'kind = "executable"',
            'source = "src"',
            'entry = "src/main.rz"',
            "",
            "[dependencies]",
            f'lexer = "{lexer}"',
            "",
            "[profile.debug]",
            "optimization = 0",
            "debug = true",
            "incremental = false",
            "",
        ]),
        encoding="utf-8",
        newline="\n",
    )
    (work / "src" / "main.rz").write_text(
        r'''// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

import lexer::lexer;

fn main() -> i64 {
    i64 bytes = raz_compiler_rt_arena_create_width(4, 1);
    if (bytes == 0) { return 1; }
    i64 stable = bytes;

    raz_compiler_rt_arena_set(bytes, 0, 17);
    raz_compiler_rt_arena_set(bytes, 1, 34);
    raz_compiler_rt_arena_set(bytes, 2, 51);
    raz_compiler_rt_arena_set(bytes, 3, 68);

    // A compact byte arena must never be accepted by an i64-cell native API.
    if (raz_compiler_rt_arena_native_address(bytes, 8) != 0) { return 2; }
    if (raz_compiler_rt_arena_native_address(bytes, 1) == 0) { return 3; }

    // Resize may relocate payload storage but must never change the public handle.
    if (raz_compiler_rt_arena_resize(bytes, 131072) != stable) { return 4; }
    if (
        raz_compiler_rt_arena_get(bytes, 0) != 17 ||
        raz_compiler_rt_arena_get(bytes, 1) != 34 ||
        raz_compiler_rt_arena_get(bytes, 2) != 51 ||
        raz_compiler_rt_arena_get(bytes, 3) != 68
    ) { return 5; }
    if (raz_compiler_rt_arena_get(bytes, 65536) != 0) { return 6; }
    raz_compiler_rt_arena_set(bytes, 131071, 255);
    if (raz_compiler_rt_arena_get(bytes, 131071) != 255) { return 7; }

    raz_compiler_rt_arena_destroy(bytes);
    // Tombstoned handles fail closed and a double destroy is harmless.
    if (raz_compiler_rt_arena_get(bytes, 0) != 0) { return 8; }
    if (raz_compiler_rt_arena_native_address(bytes, 1) != 0) { return 9; }
    if (raz_compiler_rt_arena_resize(bytes, 16) != 0) { return 10; }
    raz_compiler_rt_arena_destroy(bytes);

    // Descriptors are not recycled, so a stale handle cannot alias a later arena.
    i64 later = raz_compiler_rt_arena_create_width(4, 1);
    if (later == 0 || later == stable) { return 11; }
    raz_compiler_rt_arena_destroy(later);

    i64 cells = raz_compiler_rt_arena_create(8);
    if (cells == 0) { return 12; }
    if (raz_compiler_rt_arena_native_address(cells, 8) == 0) { return 13; }
    if (raz_compiler_rt_arena_native_address(cells, 1) != 0) { return 14; }
    raz_compiler_rt_arena_set_unchecked(cells, 3, 123456789);
    if (raz_compiler_rt_arena_get_unchecked(cells, 3) != 123456789) { return 15; }
    raz_compiler_rt_arena_destroy(cells);
    if (raz_compiler_rt_arena_get_unchecked(cells, 3) != 0) { return 16; }
    return 0;
}
''',
        encoding="utf-8",
        newline="\n",
    )

    build = subprocess.run(
        [str(raz), "build", "raz.toml"],
        cwd=work,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if build.returncode != 0:
        fail(f"build returned {build.returncode}\nstdout:\n{build.stdout}\nstderr:\n{build.stderr}")

    suffix = ".exe" if sys.platform == "win32" else ""
    executable = work / "target" / "debug" / "bin" / f"compiler-arena-abi{suffix}"
    if not executable.is_file():
        fail(f"executable missing: {executable}")
    run = subprocess.run([str(executable)], cwd=work, check=False)
    if run.returncode != 0:
        fail(f"runtime returned {run.returncode}")

    lexer_source = (ROOT / "compiler/src/raz_lexer/src/lexer/lexer.rz").read_text(encoding="utf-8")
    compiler_main = (ROOT / "compiler/src/raz_driver/src/driver/compiler_main.rz").read_text(encoding="utf-8")
    web_bundle = (ROOT / "compiler/src/raz_driver/src/driver/web_bundle.rz").read_text(encoding="utf-8")
    if "return descriptor as i64;" not in lexer_source or "raz_compiler_rt_arena_dead_magic" not in lexer_source:
        fail("stable descriptor/tombstone arena model is missing")
    if "raz_rt_read_ascii_i64(" in compiler_main:
        fail("compiler source ingestion regressed to the fixed-width i64 file ABI")
    if "raz_compiler_rt_arena_native_address(data, 1)" not in web_bundle:
        fail("web hashing no longer unwraps byte-arena payload explicitly")

    print("compiler-arena-abi: PASS (stable handles + width-safe native boundary + tombstones + resize preservation)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
