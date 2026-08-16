#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
from pathlib import Path
from compiler_test_driver import build_test_compiler


def run(args: list[str], cwd: Path, env: dict[str, str]) -> subprocess.CompletedProcess[str]:
    proc = subprocess.run(args, cwd=cwd, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if proc.returncode != 0:
        raise RuntimeError(f"failed {proc.returncode}: {' '.join(args)}\n{proc.stdout}\n{proc.stderr}")
    return proc


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler")
    parser.add_argument("--raz")
    parser.add_argument("--root")
    parser.add_argument("--linker")
    parser.add_argument("--work", required=True)
    args = parser.parse_args()

    work = Path(args.work).resolve()
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    env = os.environ.copy()

    if args.compiler:
        compiler = str(Path(args.compiler).resolve())
    else:
        if not args.raz or not args.root or not args.linker:
            raise SystemExit("provide --compiler or --raz/--root/--linker")
        root = Path(args.root).resolve()
        compiler = str(build_test_compiler(root, work, args.raz, args.linker, env))

    index = work / "index.txt"
    index.write_text(
        "json-tools 0.9.0 packages/json-tools/0.9.0.dpk 0000000000000001\n"
        "widget 1.0.0 packages/widget/1.0.0.dpk 0000000000000002\n"
        "widget 1.2.0 packages/widget/1.2.0.dpk 0000000000000003\n"
        "widget 2.0.0 packages/widget/2.0.0.dpk 0000000000000004\n",
        encoding="utf-8",
    )

    env["RAZ_REGISTRY_INDEX"] = str(index)
    env["RAZ_HOME"] = str(work / "home")

    search = run([compiler, "search", "WID"], work, env)
    assert search.stdout == "widget@2.0.0\n", search.stdout

    search_json = run([compiler, "search", "json"], work, env)
    assert search_json.stdout == "json-tools@0.9.0\n", search_json.stdout

    missing = run([compiler, "search", "missing"], work, env)
    assert missing.stdout == "No packages found.\n", missing.stdout

    info = run([compiler, "info", "widget"], work, env)
    assert "name: widget\n" in info.stdout, info.stdout
    assert "latest: 2.0.0\n" in info.stdout, info.stdout
    for version in ("1.0.0", "1.2.0", "2.0.0"):
        assert f"  {version}\n" in info.stdout, info.stdout

    project = work / "project"
    dep = project / "deps" / "widget"
    dep.mkdir(parents=True)
    (project / "raz.toml").write_text(
        '[package]\nname = "app"\nversion = "0.1.0"\nkind = "executable"\n\n'
        '[dependencies]\nwidget = "./deps/widget"\n',
        encoding="utf-8",
    )
    (dep / "raz.toml").write_text(
        '[package]\nname = "widget"\nversion = "1.1.0"\nkind = "library"\n\n[dependencies]\n',
        encoding="utf-8",
    )
    (project / ".raz.registry").write_text('widget = "widget@^1.0.0"\n', encoding="utf-8")

    outdated = run([compiler, "outdated"], project, env)
    assert outdated.stdout == "widget  1.1.0 -> 2.0.0 (compatible 1.2.0)\n", outdated.stdout

    (dep / "raz.toml").write_text(
        '[package]\nname = "widget"\nversion = "2.0.0"\nkind = "library"\n\n[dependencies]\n',
        encoding="utf-8",
    )
    current = run([compiler, "outdated"], project, env)
    assert current.stdout == "All registry dependencies are up to date.\n", current.stdout

    print("registry discovery: PASS (search + info + outdated)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
