#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Executable release qualification for combined Raz Web production features."""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def run(cmd: list[str], cwd: Path, env: dict[str, str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, cwd=cwd, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--raz", required=True)
    ap.add_argument("--work-root", required=True)
    args = ap.parse_args()

    raz = Path(args.raz).resolve()
    work = Path(args.work_root).resolve()
    shutil.rmtree(work, ignore_errors=True)
    project = work / "production-hardening"
    shutil.copytree(ROOT / "tests" / "examples" / "web" / "production-hardening", project)

    env = os.environ.copy()
    env["RAZ_HOME"] = str(ROOT)
    runtime = ROOT / "build" / "release" / "src" / "runtime" / "libraz_runtime.a"
    if runtime.is_file():
        env["RAZ_RUNTIME_LIBRARY"] = str(runtime)

    built = run([str(raz), "build", "--release", "--analyze", "raz.toml"], project, env)
    if built.returncode != 0:
        print(built.stdout)
        return 1

    assets = project / "dist" / "assets"
    js_files = sorted(assets.glob("app.*.js"))
    wasm_files = sorted(assets.glob("app.*.wasm"))
    if len(js_files) != 1 or len(wasm_files) != 1:
        print("web-production-hardening-runtime: expected one fingerprinted app JS and Wasm")
        return 1
    js = js_files[0].read_text(encoding="utf-8")
    report = (project / "target" / "release" / "web-bundle-analysis.txt").read_text(encoding="utf-8")

    required = (
        "const patchChildren =",
        "const dispatchNode =",
        "const applyStateBindings =",
        "const renderOwnedScope =",
        "const syncRouteChange =",
        "const pumpHttp = async () =>",
        "addEventListener('popstate'",
        "const razWeb = {",
    )
    missing = [token for token in required if token not in js]
    if missing:
        print(f"web-production-hardening-runtime: combined runtime missing {missing}")
        return 1
    if js.count("root.addEventListener('click'") != 1:
        print("web-production-hardening-runtime: combined event+routing runtime did not keep one click listener")
        return 1
    if "__RAZ_WEB_HOST_BEGIN_" in js or "__RAZ_WEB_HOST_END_" in js:
        print("web-production-hardening-runtime: private host-pruning markers leaked into release JS")
        return 1
    if "Budgets:" not in report or "EXCEEDED" in report:
        print("web-production-hardening-runtime: configured release budgets were not reported as passing")
        return 1

    checked = subprocess.run(["node", "--check", str(js_files[0])], text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if checked.returncode != 0:
        print(checked.stdout)
        return 1

    print(
        "web-production-hardening-runtime: PASS "
        f"(js={js_files[0].stat().st_size}B wasm={wasm_files[0].stat().st_size}B; routing+state+effects+HTTP combined)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
