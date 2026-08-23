#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import argparse
from pathlib import Path
import struct
import subprocess
import sys


def run(command: list[str], *, stdout_path: Path | None = None) -> str:
    result = subprocess.run(command, text=True, capture_output=True)
    if result.returncode != 0:
        print("command failed:", " ".join(command), file=sys.stderr)
        if result.stdout:
            print(result.stdout, file=sys.stderr)
        if result.stderr:
            print(result.stderr, file=sys.stderr)
        raise SystemExit(result.returncode or 1)
    if stdout_path is not None:
        stdout_path.write_text(result.stdout, encoding="utf-8")
    return result.stdout


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(f"Forge AArch64 cross-object qualification failed: {message}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--razc", required=True)
    parser.add_argument("--forge-codegen", required=True)
    parser.add_argument("--work", required=True)
    args = parser.parse_args()

    work = Path(args.work)
    work.mkdir(parents=True, exist_ok=True)
    source = work / "cross_object.rz"
    fir = work / "cross_object.fir"
    source.write_text(
        "fn main() -> i64 {\n"
        "    i64 left = 17;\n"
        "    i64 right = 25;\n"
        "    return left + right;\n"
        "}\n",
        encoding="utf-8",
    )

    fir_text = run([args.razc, "--forge-ir", str(source)], stdout_path=fir)
    require("func @main" in fir_text and "add i64" in fir_text,
            "Raz frontend did not produce the expected structured Forge IR")

    elf_a = work / "cross-a.elf.o"
    elf_b = work / "cross-b.elf.o"
    macho_a = work / "cross-a.macho.o"
    macho_b = work / "cross-b.macho.o"
    for output in (elf_a, elf_b):
        run([args.forge_codegen, str(fir), "--arch=aarch64", f"--emit-elf={output}"])
    for output in (macho_a, macho_b):
        run([args.forge_codegen, str(fir), "--arch=aarch64", f"--emit-macho={output}"])

    elf = elf_a.read_bytes()
    require(elf == elf_b.read_bytes(), "ELF64 AArch64 emission is not deterministic")
    require(len(elf) >= 20 and elf[:4] == b"\x7fELF", "ELF object magic is invalid")
    require(elf[4] == 2 and elf[5] == 1, "ELF object is not 64-bit little-endian")
    require(struct.unpack_from("<H", elf, 18)[0] == 183, "ELF e_machine is not EM_AARCH64")

    macho = macho_a.read_bytes()
    require(macho == macho_b.read_bytes(), "Mach-O arm64 emission is not deterministic")
    require(len(macho) >= 12 and macho[:4] == b"\xcf\xfa\xed\xfe", "Mach-O 64 magic is invalid")
    require(struct.unpack_from("<I", macho, 4)[0] == 0x0100000C, "Mach-O cputype is not CPU_TYPE_ARM64")

    print("Raz -> Forge IR -> AArch64 ELF/Mach-O cross-object qualification: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
