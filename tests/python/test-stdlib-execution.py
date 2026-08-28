#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Execute maintained Raz standard-library smokes through the current Forge path.

The cases compose the real library modules with tiny test drivers, lower them
through the production Forge backend, emit native objects with Forge, then link
and execute against the production Raz runtime.
"""
from __future__ import annotations

import argparse
from collections import deque
import os
import re
from pathlib import Path
import shutil
import subprocess
import sys
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

IMPORT_RE = re.compile(r"^\s*import\s+([A-Za-z_][A-Za-z0-9_:]*)\s*;", re.MULTILINE)
NAMESPACE_RE = re.compile(r"^\s*namespace\s+([A-Za-z_][A-Za-z0-9_:]*)\s*;", re.MULTILINE)


def stdlib_namespace_map() -> dict[str, Path]:
    mapping: dict[str, Path] = {}
    for module in sorted((ROOT / "library").rglob("*.rz")):
        text = module.read_text(encoding="utf-8")
        match = NAMESPACE_RE.search(text)
        if match:
            mapping[match.group(1)] = module
    return mapping


def module_closure(seed_paths: list[Path]) -> list[Path]:
    mapping = stdlib_namespace_map()
    selected: dict[Path, None] = {}
    pending: deque[Path] = deque(seed_paths)
    while pending:
        module = pending.popleft().resolve()
        if module in selected:
            continue
        selected[module] = None
        text = module.read_text(encoding="utf-8")
        for namespace in IMPORT_RE.findall(text):
            dependency = mapping.get(namespace)
            if dependency is not None and dependency.resolve() not in selected:
                pending.append(dependency)
    return list(selected)


SMOKE_RUNTIME_DECLARATIONS: dict[str, str] = {
    "raz_rt_alloc": "extern fn raz_rt_alloc(i64 size) -> usize;",
    "raz_rt_dealloc": "extern fn raz_rt_dealloc(usize pointer);",
    "raz_rt_file_close": "extern fn raz_rt_file_close(usize handle);",
    "raz_rt_file_open": "extern fn raz_rt_file_open(usize path, i64 length, i64 flags) -> usize;",
    "raz_rt_file_seek": "extern fn raz_rt_file_seek(usize handle, i64 offset, i64 origin) -> i64;",
    "raz_rt_load_u8": "extern fn raz_rt_load_u8(usize address) -> i64;",
    "raz_rt_remove_one": "extern fn raz_rt_remove_one(usize path, i64 length) -> i64;",
    "raz_rt_store_u8": "extern fn raz_rt_store_u8(usize address, i64 value) -> i64;",
    "raz_rt_volatile_load_i64": "extern fn raz_rt_volatile_load_i64(usize address) -> i64;",
    "raz_rt_volatile_store_i64": "extern fn raz_rt_volatile_store_i64(usize address, i64 value);",
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


def resolve_forge_cli(forge_codegen: Path) -> Path:
    """Resolve Forge 2.0's compiler CLI from any shipped Forge tool path."""
    name = forge_codegen.name.lower()
    if name in {"forge-run", "forge-run.exe"}:
        sibling = forge_codegen.with_name("forge.exe" if name.endswith(".exe") else "forge")
        if sibling.is_file():
            return sibling
        raise SystemExit(f"stdlib-execution: sibling Forge compiler is missing: {sibling}")
    return forge_codegen


def compose(case: str, output: Path) -> None:
    chunks: list[str] = []
    seeds = [ROOT / rel for rel in CASES[case]]
    for module in module_closure(seeds):
        chunks.append(module.read_text(encoding="utf-8"))
    driver = (ROOT / "tests" / "stdlib" / f"{case}_smoke.rz").read_text(encoding="utf-8")
    prefix = "namespace __raz_stdlib_smoke;\n"
    if case in SMOKE_IMPORTS:
        prefix += f"import {SMOKE_IMPORTS[case]};\n"
    chunks.append(prefix + "\n" + driver)
    output.write_text("\n\n".join(chunks) + "\n", encoding="utf-8")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--razc", required=True, type=Path)
    # Retained for release-gate CLI compatibility. The production package
    # build owns Forge object emission and runtime linking end to end.
    ap.add_argument("--forge-codegen", required=True, type=Path)
    ap.add_argument("--runtime", required=True, type=Path)
    ap.add_argument("--timeout", type=int, default=180)
    args = ap.parse_args()

    with tempfile.TemporaryDirectory(prefix="raz-stdlib-exec-") as directory:
        project = Path(directory) / "suite"
        source_dir = project / "src"
        smoke_dir = source_dir / "smokes"
        smoke_dir.mkdir(parents=True)
        package_name = "stdlib_execution_suite"
        (project / "raz.toml").write_text(
            "[package]\n"
            f'name = "{package_name}"\n'
            'version = "1.0.0"\n'
            'kind = "executable"\n'
            'entry = "src/main.rz"\n\n'
            '[dependencies]\n'
            f'core = "{(ROOT / "library" / "core").as_posix()}"\n'
            f'alloc = "{(ROOT / "library" / "alloc").as_posix()}"\n'
            f'collections = "{(ROOT / "library" / "collections").as_posix()}"\n'
            f'std = "{(ROOT / "library" / "std").as_posix()}"\n',
            encoding="utf-8",
        )

        entry_imports: list[str] = []
        entry_checks: list[str] = []
        for index, case in enumerate(CASES, start=1):
            namespace = f"__raz_stdlib_smoke::{case}"
            entry_imports.append(f"import {namespace};")
            entry_checks.append(
                f"    if ({namespace}::run() != 0) {{ return {index}; }}"
            )
            driver = (ROOT / "tests" / "stdlib" / f"{case}_smoke.rz").read_text(encoding="utf-8")
            if "fn main() -> i64" not in driver:
                raise SystemExit(f"stdlib-execution: smoke has no canonical main: {case}")
            driver = driver.replace("fn main() -> i64", "public fn run() -> i64", 1)
            prefix = f"namespace {namespace};\n"
            for runtime_name, declaration in SMOKE_RUNTIME_DECLARATIONS.items():
                if (
                    re.search(rf"\b{re.escape(runtime_name)}\s*\(", driver) and
                    f"extern fn {runtime_name}" not in driver
                ):
                    prefix += declaration + "\n"
            imported_namespaces: set[str] = set()
            for rel in CASES[case]:
                module_text = (ROOT / rel).read_text(encoding="utf-8")
                match = NAMESPACE_RE.search(module_text)
                if match and match.group(1) not in imported_namespaces:
                    prefix += f"import {match.group(1)};\n"
                    imported_namespaces.add(match.group(1))
            if case in SMOKE_IMPORTS:
                extra_imports = SMOKE_IMPORTS[case].replace(";\nimport ", ";\nimport ")
                prefix += f"import {extra_imports};\n"
            (smoke_dir / f"{case}.rz").write_text(prefix + "\n" + driver, encoding="utf-8")

        (source_dir / "main.rz").write_text(
            "\n".join(entry_imports)
            + "\n\nfn main() -> i64 {\n"
            + "\n".join(entry_checks)
            + "\n    return 0;\n}\n",
            encoding="utf-8",
        )

        result = run(
            [
                str(args.razc), "build",
                "--backend=forge", "--profile", "debug", "--force",
                str(project),
            ],
            timeout=args.timeout,
        )
        if result.returncode != 0:
            fail("aggregate Forge package build", result)

        executable = project / "target" / "debug" / "bin" / package_name
        if os.name == "nt":
            executable = executable.with_suffix(".exe")
        if not executable.is_file():
            raise SystemExit(f"stdlib-execution: missing executable: {executable}")
        result = run([str(executable)], timeout=args.timeout)
        if result.returncode != 0:
            index = result.returncode
            names = list(CASES)
            case = names[index - 1] if 1 <= index <= len(names) else "unknown"
            fail(f"aggregate Forge run case={case} index={index}", result)

        for case in CASES:
            print(f"stdlib-execution: PASS {case} (Forge=0)")

    print(f"stdlib-execution: PASS ({len(CASES)} cases in one package graph)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
