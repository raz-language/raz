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


def run(args: list[str], cwd: Path, env: dict[str, str], expect: int = 0) -> subprocess.CompletedProcess[str]:
    p = subprocess.run(args, cwd=cwd, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if p.returncode != expect:
        raise RuntimeError(
            f"command failed {p.returncode} expected {expect}: {' '.join(args)}\n"
            f"stdout:\n{p.stdout}\nstderr:\n{p.stderr}"
        )
    return p


def write_library(root: Path) -> None:
    (root / "src").mkdir(parents=True, exist_ok=True)
    (root / "raz.toml").write_text(
        '''[package]\nname = "core"\nversion = "1.0.0"\nkind = "library"\nentry = "src/lib.rz"\n\n[dependencies]\n''',
        encoding="utf-8",
    )
    (root / "src/lib.rz").write_text(
        "namespace core;\npublic fn value() -> i64 { return 42; }\n",
        encoding="utf-8",
    )


def write_app(root: Path) -> None:
    (root / "src").mkdir(parents=True, exist_ok=True)
    (root / "raz.toml").write_text(
        '''[package]\nname = "app"\nversion = "1.0.0"\nkind = "executable"\nentry = "src/main.rz"\n\n[dependencies]\ncore = "../core"\n''',
        encoding="utf-8",
    )
    (root / "src/main.rz").write_text(
        "import core;\nfn main() -> i64 { return core::value() - 42; }\n",
        encoding="utf-8",
    )


def build_compiler(ns: argparse.Namespace, work: Path, env: dict[str, str]) -> Path:
    if ns.compiler:
        return Path(ns.compiler).resolve()
    return build_test_compiler(Path(ns.root).resolve(), work, ns.raz, ns.linker, env)


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
    compiler = build_compiler(ns, work, env)
    c = str(compiler)

    ws = work / "workspace"
    write_library(ws / "core")
    write_app(ws / "app")
    (ws / "raz.toml").write_text(
        '''[workspace]\nmembers = [\n  "core",\n  # comments and trailing commas are accepted\n  "app",\n]\n''',
        encoding="utf-8",
    )

    check = run([c, "check", "--workspace"], ws, env)
    assert "workspace: core" in check.stdout and "workspace: app" in check.stdout
    run([c, "build", "--workspace"], ws, env)
    run([c, "test", "--workspace"], ws, env)
    run([c, "lock", "--workspace"], ws, env)

    lock = (ws / "raz.lock").read_text(encoding="utf-8")
    assert "workspace = true" in lock
    assert lock.count("[[package]]") == 2, lock
    assert 'name = "core"' in lock and 'path = "core"' in lock
    assert 'name = "app"' in lock and 'path = "app"' in lock
    assert not (ws / "core/raz.lock").exists()
    assert not (ws / "app/raz.lock").exists()

    metadata = run([c, "metadata", "--workspace"], ws, env).stdout
    assert "workspace: core" in metadata and "package=core" in metadata
    assert "workspace: app" in metadata and "dependency=core=../core" in metadata
    graph = run([c, "graph", "--workspace"], ws, env).stdout
    assert "workspace: core" in graph and "core@1.0.0" in graph
    assert "workspace: app" in graph and "app@1.0.0" in graph and "|- core@1.0.0" in graph

    # Fetch/update execute in each member directory, then restore one root lock.
    run([c, "fetch", "--workspace"], ws, env)
    run([c, "update", "--workspace"], ws, env)
    assert (ws / "raz.lock").exists()
    assert not (ws / "core/raz.lock").exists()
    assert not (ws / "app/raz.lock").exists()

    # The root manifest may be named explicitly, but other positional paths are rejected.
    run([c, "check", "--workspace", "raz.toml"], ws, env)
    run([c, "check", "--workspace", "other.toml"], ws, env, expect=55)
    run([c, "run", "--workspace"], ws, env, expect=55)

    invalid = work / "invalid"
    invalid.mkdir()
    (invalid / "raz.toml").write_text('[workspace]\nmembers = ["../outside"]\n', encoding="utf-8")
    run([c, "lock", "--workspace"], invalid, env, expect=57)
    (invalid / "raz.toml").write_text('[workspace]\nmembers = ["same", "same"]\n', encoding="utf-8")
    run([c, "lock", "--workspace"], invalid, env, expect=57)

    print("package-workspaces: PASS (members + build/check/test + root lock + metadata/graph + update/fetch)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
