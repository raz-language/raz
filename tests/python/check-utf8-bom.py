#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""A leading UTF-8 BOM must not change how a manifest or a `.rz` module compiles.

Windows tooling writes one by default -- Notepad, VS Code's "UTF-8 with BOM"
setting, and Windows PowerShell 5.1's `Set-Content -Encoding utf8`, `Out-File
-Encoding utf8`, and `>` redirection all emit EF BB BF -- so a BOM-prefixed
`raz.toml` or module has to behave exactly like its BOM-less twin. Before the
toolchain skipped the marker, a BOM on `raz.toml` hid the package name from the
manifest scan and surfaced as `could not prepare target output paths` (exit 40,
no `target/`), while a BOM on a module produced an `expected a different token`
diagnostic pointing at an invisible character.

Fixtures are written with `write_bytes` so the marker is explicit in the test
rather than an artifact of Python's encoder.
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
from pathlib import Path

from compiler_test_driver import build_test_compiler

BOM = b"\xef\xbb\xbf"
MANIFEST = (
    b"[package]\n"
    b'name = "smoke"\n'
    b'version = "0.1.0"\n'
    b'kind = "executable"\n'
    b'source = "src"\n'
    b'entry = "src/main.rz"\n'
    b"\n"
    b"[dependencies]\n"
)
SOURCE = b"fn main() -> i64 { return 7; }\n"
ARTIFACT = "smoke.exe" if os.name == "nt" else "smoke"
CASES = {
    "baseline": (False, False),
    "manifest-bom": (True, False),
    "source-bom": (False, True),
    "manifest-and-source-bom": (True, True),
}


def run(args: list[str], cwd: Path, env: dict[str, str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(args, cwd=cwd, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def fail(label: str, what: str, result: subprocess.CompletedProcess[str]) -> RuntimeError:
    return RuntimeError(
        f"{label}: {what} ({result.returncode})\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )


def write_project(root: Path, *, manifest_bom: bool, source_bom: bool) -> None:
    shutil.rmtree(root, ignore_errors=True)
    (root / "src").mkdir(parents=True)
    (root / "raz.toml").write_bytes((BOM if manifest_bom else b"") + MANIFEST)
    (root / "src" / "main.rz").write_bytes((BOM if source_bom else b"") + SOURCE)


def write_cases(root: Path) -> dict[str, Path]:
    projects = {}
    for label, (manifest_bom, source_bom) in CASES.items():
        project = root / label
        write_project(project, manifest_bom=manifest_bom, source_bom=source_bom)
        projects[label] = project
    return projects


def check_production_compiler(compiler: str, work: Path, env: dict[str, str]) -> None:
    projects = write_cases(work / "compiler")
    for label, project in projects.items():
        result = run([compiler, "check", "raz.toml"], project, env)
        if result.returncode != 0:
            raise fail(label, "check rejected a byte-order mark", result)

    def build(project: Path) -> tuple[subprocess.CompletedProcess[str], Path]:
        return run([compiler, "build"], project, env), project / "target" / "debug" / ARTIFACT

    # A BOM-less project that cannot link says the native toolchain is missing,
    # not that the compiler mishandled the marker; keep the check-level coverage
    # in that case rather than reporting an unrelated environment failure.
    baseline, artifact = build(projects["baseline"])
    if baseline.returncode != 0 or not artifact.is_file():
        print("UTF-8 BOM inputs: native linking unavailable; verified with check only")
        return

    for label, project in projects.items():
        result, artifact = build(project)
        if result.returncode != 0:
            raise fail(label, "build rejected a byte-order mark", result)
        if not artifact.is_file():
            raise RuntimeError(f"{label}: build produced no native artifact at {artifact}")
        exit_code = subprocess.run([str(artifact)], cwd=project).returncode
        if exit_code != 7:
            raise RuntimeError(f"{label}: artifact returned {exit_code}, expected 7")


def check_host_compiler(host: str, work: Path, env: dict[str, str]) -> None:
    # The bootstrap driver reads manifests and modules through its own C++
    # readers, and it stitches module bodies into a generated semantic unit, so
    # it needs the same coverage. `check` exercises both without linking.
    projects = write_cases(work / "host")
    for label, project in projects.items():
        result = run([host, "check", str(project), "--target", "host", "--profile", "debug"], work, env)
        if result.returncode != 0:
            raise fail(f"host {label}", "check rejected a byte-order mark", result)


def check_direct_source(compiler: str, work: Path, env: dict[str, str]) -> None:
    # A BOM on a single file compiled straight from the command line has to be
    # skipped as well -- that path never reads a manifest.
    direct = work / "direct.rz"
    direct.write_bytes(BOM + SOURCE)
    result = run([compiler, "check", str(direct)], work, env)
    if result.returncode != 0:
        raise fail("direct source", "check rejected a byte-order mark", result)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--compiler")
    ap.add_argument("--raz")
    ap.add_argument("--root")
    ap.add_argument("--work", required=True)
    ap.add_argument("--linker")
    ns = ap.parse_args()

    work = Path(ns.work).resolve()
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    env = os.environ.copy()
    compiler = str(
        Path(ns.compiler).resolve()
        if ns.compiler
        else build_test_compiler(Path(ns.root).resolve(), work, ns.raz, ns.linker, env)
    )

    check_production_compiler(compiler, work, env)
    check_direct_source(compiler, work, env)
    if ns.raz:
        check_host_compiler(ns.raz, work, env)

    print("UTF-8 BOM inputs: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
