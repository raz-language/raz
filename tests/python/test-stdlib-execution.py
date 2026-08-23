#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Execute maintained Raz standard-library smokes through the current Forge path.

The cases compose the real library modules with tiny test drivers, lower them
with ``razc --forge-ir``, emit native objects with Forge, then link and execute
against the production Raz runtime.
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]

CASES: dict[str, list[str]] = {
    "arena": ["library/alloc/arena/arena.rz"],
    "pool": ["library/alloc/pool/pool.rz"],
    "path": ["library/std/path/path.rz"],
    "string": ["library/core/utf8/utf8.rz", "library/alloc/string/string.rz"],
    "vec": ["library/alloc/vec/vec.rz"],
    "deque": ["library/alloc/deque/deque.rz"],
    "hash_set": ["library/alloc/hash_set/hash_set.rz"],
    "hash_map": ["library/alloc/hash_map/hash_map.rz"],
    "file": ["library/core/result/result.rz", "library/std/io/error.rz", "library/std/fs/file.rz"],
    "buffered": ["library/std/io/buffered.rz"],
    "udp": ["library/core/result/result.rz", "library/std/io/error.rz", "library/std/net/net.rz"],
    "math": ["library/std/math/math.rz"],
    "fmt": ["library/alloc/string/string.rz", "library/std/fmt/fmt.rz"],
    "stdio_text": ["library/alloc/string/string.rz", "library/std/fmt/fmt.rz", "library/std/io/stdio.rz"],
    "testing": ["library/alloc/string/string.rz", "library/std/fmt/fmt.rz", "library/std/io/stdio.rz", "library/std/testing/testing.rz"],
    "utf8": ["library/core/bytes/bytes.rz", "library/core/utf8/utf8.rz"],
    "system": ["library/std/os/system/system.rz"],
    "os_memory": ["library/std/os/memory/memory.rz"],
    "fs_platform": ["library/core/result/result.rz", "library/std/io/error.rz", "library/std/fs/platform.rz"],
    "io_error": ["library/std/io/error.rz"],
    "net_controls": ["library/core/result/result.rz", "library/std/io/error.rz", "library/std/net/net.rz"],
    "spsc_stress": ["library/std/thread/spsc/spsc.rz"],
    "mpmc_stress": ["library/std/thread/mpmc/mpmc.rz"],
    "hash_set_stress": ["library/alloc/hash_set/hash_set.rz"],
    "hash_map_stress": ["library/alloc/hash_map/hash_map.rz"],
    "arena_boundary": ["library/alloc/arena/arena.rz"],
    "net_partial_io": ["library/core/result/result.rz", "library/std/io/error.rz", "library/std/net/net.rz"],
    "mpmc_contention": ["library/core/atomic/atomic.rz", "library/alloc/box/box.rz", "library/std/thread/thread.rz", "library/std/thread/mpmc/mpmc.rz"],
    "hash_map_model": ["library/alloc/hash_map/hash_map.rz"],
    "hash_set_model": ["library/alloc/hash_set/hash_set.rz"],
    "channel_contention": ["library/core/atomic/atomic.rz", "library/alloc/box/box.rz", "library/std/sync/mutex.rz", "library/std/sync/condition.rz", "library/std/thread/cancellation.rz", "library/std/time/time.rz", "library/std/thread/thread.rz", "library/std/thread/channel.rz"],
    "future_contention": ["library/alloc/box/box.rz", "library/std/sync/mutex.rz", "library/std/sync/condition.rz", "library/std/time/time.rz", "library/std/thread/thread.rz", "library/std/thread/future.rz"],
    "allocator_boundary": ["library/alloc/box/box.rz", "library/alloc/pool/pool.rz", "library/alloc/arena/arena.rz"],
}

SMOKE_IMPORTS: dict[str, str] = {
    "arena": "alloc::arena",
    "pool": "alloc::pool",
    "path": "std::path",
    "string": "alloc::string",
    "vec": "alloc::vec",
    "deque": "alloc::deque",
    "hash_set": "alloc::hash_set",
    "hash_map": "alloc::hash_map",
    "file": "std::fs::file",
    "buffered": "std::io::buffered",
    "udp": "std::net",
    "math": "std::math",
    "fmt": "std::fmt",
    "stdio_text": "std::io::stdio",
    "testing": "std::testing",
    "utf8": "core::utf8",
    "system": "std::os::system",
    "os_memory": "std::os::memory",
    "fs_platform": "std::fs::platform;\nimport core::result;\nimport std::io::error",
    "io_error": "std::io::error",
    "net_controls": "std::net",
    "spsc_stress": "std::thread::spsc",
    "mpmc_stress": "std::thread::mpmc",
    "hash_set_stress": "alloc::hash_set",
    "hash_map_stress": "alloc::hash_map",
    "arena_boundary": "alloc::arena",
    "net_partial_io": "std::net",
    "mpmc_contention": "core::atomic;\nimport alloc::box;\nimport std::thread;\nimport std::thread::mpmc",
    "hash_map_model": "alloc::hash_map",
    "hash_set_model": "alloc::hash_set",
    "channel_contention": "core::atomic;\nimport alloc::box;\nimport std::thread;\nimport std::thread::channel",
    "future_contention": "alloc::box;\nimport std::thread;\nimport std::thread::future",
    "allocator_boundary": "alloc::box;\nimport alloc::pool;\nimport alloc::arena",
}


def run(cmd: list[str], *, timeout: int = 60) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout)


def fail(label: str, result: subprocess.CompletedProcess[str]) -> None:
    print(f"stdlib-execution: FAIL {label}")
    if result.stdout:
        print(result.stdout, end="")
    if result.stderr:
        print(result.stderr, end="")
    raise SystemExit(1)


def compose(case: str, output: Path) -> None:
    chunks: list[str] = []
    for rel in CASES[case]:
        chunks.append((ROOT / rel).read_text(encoding="utf-8"))
    driver = (ROOT / "tests" / "stdlib" / f"{case}_smoke.rz").read_text(encoding="utf-8")
    prefix = "namespace __raz_stdlib_smoke;\n"
    if case in SMOKE_IMPORTS:
        prefix += f"import {SMOKE_IMPORTS[case]};\n"
    chunks.append(prefix + "\n" + driver)
    output.write_text("\n\n".join(chunks) + "\n", encoding="utf-8")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--razc", required=True, type=Path)
    ap.add_argument("--forge-codegen", required=True, type=Path)
    ap.add_argument("--runtime", required=True, type=Path)
    ap.add_argument("--runtime-link-manifest", type=Path, help="CMake-generated runtime dependency manifest")
    ap.add_argument("--timeout", type=int, default=60)
    args = ap.parse_args()

    linker = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("c++") or shutil.which("g++")
    if not linker:
        raise SystemExit("stdlib-execution: no C++ linker driver found")

    manifest = args.runtime_link_manifest
    if manifest is None:
        # runtime normally lives at <build>/src/runtime/libraz_runtime.*
        candidate = args.runtime.resolve().parents[2] / "raz-runtime-link-deps.txt"
        if candidate.exists():
            manifest = candidate
    runtime_link_deps: list[str] = []
    if manifest is not None and manifest.exists():
        runtime_link_deps = [
            line.strip()
            for line in manifest.read_text(encoding="utf-8").splitlines()
            if line.strip()
        ]

    with tempfile.TemporaryDirectory(prefix="raz-stdlib-exec-") as directory:
        work = Path(directory)
        for case in CASES:
            source = work / f"{case}.rz"
            compose(case, source)

            fir = work / f"{case}.fir"
            forge_object = work / (f"{case}.obj" if os.name == "nt" else f"{case}.o")
            forge_exe = work / (f"{case}-forge.exe" if os.name == "nt" else f"{case}-forge")
            result = run([str(args.razc), "--forge-ir", str(source)], timeout=args.timeout)
            if result.returncode != 0:
                fail(f"{case} Forge compile", result)
            fir.write_text(result.stdout, encoding="utf-8")
            if os.name == "nt":
                emit = [str(args.forge_codegen), str(fir), f"--emit-coff={forge_object}", "--abi=windows"]
            else:
                emit = [str(args.forge_codegen), str(fir), f"--emit-elf={forge_object}", "--abi=sysv"]
            result = run(emit, timeout=args.timeout)
            if result.returncode != 0:
                fail(f"{case} Forge object", result)
            wrapper = work / f"{case}-main.cpp"
            wrapper.write_text(
                'extern "C" long long __raz_ns___raz_stdlib_smoke__main();\n'
                'int main() { return static_cast<int>(__raz_ns___raz_stdlib_smoke__main()); }\n',
                encoding="utf-8",
            )
            link_cmd = [str(linker), str(wrapper), str(forge_object), str(args.runtime), *runtime_link_deps]
            if os.name != "nt":
                link_cmd += ["-lpthread", "-ldl"]
            link_cmd += ["-o", str(forge_exe)]
            result = run(link_cmd, timeout=args.timeout)
            if result.returncode != 0:
                fail(f"{case} Forge link", result)
            result = run([str(forge_exe)], timeout=args.timeout)
            if result.returncode != 0:
                fail(f"{case} Forge run rc={result.returncode}", result)

            print(f"stdlib-execution: PASS {case} (Forge=0)")

    print(f"stdlib-execution: PASS ({len(CASES)} cases)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
