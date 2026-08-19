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


def run(args, cwd: Path, env: dict[str, str], expect: int = 0) -> subprocess.CompletedProcess[str]:
    p = subprocess.run(args, cwd=cwd, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if p.returncode != expect:
        raise RuntimeError(
            f"command failed {p.returncode} expected {expect}: {' '.join(map(str, args))}\n"
            f"stdout:\n{p.stdout}\nstderr:\n{p.stderr}"
        )
    return p


def write_pkg(root: Path, name: str, *, executable: bool = False, body: str | None = None) -> None:
    (root / "src").mkdir(parents=True, exist_ok=True)
    entry = "src/main.rz" if executable else "src/lib.rz"
    kind = "executable" if executable else "static-library"
    (root / "raz.toml").write_text(
        f'''[package]\nname = "{name}"\nversion = "1.0.0"\nkind = "{kind}"\nentry = "{entry}"\n\n[dependencies]\n''',
        encoding="utf-8",
    )
    if body is None:
        body = f"namespace {name};\npublic fn value() -> i64 {{ return 1; }}\n" if not executable else "fn main() -> i64 { return 0; }\n"
    (root / entry).write_text(body, encoding="utf-8")


def build_compiler(ns: argparse.Namespace, work: Path, env: dict[str, str]) -> Path:
    if ns.compiler:
        return Path(ns.compiler).resolve()
    root = Path(ns.root).resolve()
    return build_test_compiler(root, work, ns.raz, ns.linker, env)


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

    # Section-aware manifest mutation and lock coverage.
    sections = work / "sections"
    app = sections / "app"
    write_pkg(app, "sections", executable=True)
    for name in ("devlib", "buildlib", "optlib", "platformlib"):
        write_pkg(sections / name, name)
    run([c, "add", "devlib", "../devlib", "--dev"], app, env)
    run([c, "add", "buildlib", "../buildlib", "--build"], app, env)
    run([c, "add", "optlib", "../optlib", "--optional"], app, env)
    target = "windows" if os.name == "nt" else ("macos" if sys.platform == "darwin" else "linux")
    run([c, "add", "platformlib", "../platformlib", f"--target={target}"], app, env)
    manifest = (app / "raz.toml").read_text(encoding="utf-8")
    for heading in ("[dev-dependencies]", "[build-dependencies]", "[optional-dependencies]", f"[target.{target}.dependencies]"):
        assert heading in manifest, heading
    lock = (app / "raz.lock").read_text(encoding="utf-8")
    for name in ("devlib", "buildlib", "optlib", "platformlib"):
        assert f'name = "{name}"' in lock, name
    run([c, "remove", "optlib"], app, env)
    assert "optlib = " not in (app / "raz.toml").read_text(encoding="utf-8")

    # Optional features, default features, and all-features control project assembly.
    feature_root = work / "features"
    extra = feature_root / "extra"
    fapp = feature_root / "app"
    write_pkg(extra, "extra", body="namespace extra;\npublic fn value() -> i64 { return 42; }\n")
    write_pkg(fapp, "feature-app", executable=True, body="import extra::extra;\nfn main() -> i64 { return extra::value() - 42; }\n")
    with (fapp / "raz.toml").open("a", encoding="utf-8") as f:
        f.write('\n[optional-dependencies]\nextra = "../extra"\n\n[features]\ndefault = []\nextra = ["dep:extra"]\n')
    run([c, "check", "raz.toml"], fapp, env, expect=43)
    run([c, "check", "--features=extra", "raz.toml"], fapp, env)
    run([c, "check", "--all-features", "raz.toml"], fapp, env)
    text = (fapp / "raz.toml").read_text(encoding="utf-8").replace("default = []", 'default = ["dep:extra"]')
    (fapp / "raz.toml").write_text(text, encoding="utf-8")
    run([c, "check", "raz.toml"], fapp, env)
    run([c, "check", "--no-default-features", "raz.toml"], fapp, env, expect=43)

    # Dev dependencies are visible to raz test but not ordinary builds/checks.
    dev_root = work / "dev"
    devlib = dev_root / "devlib"
    dapp = dev_root / "app"
    write_pkg(devlib, "devlib", body="namespace devlib;\npublic fn answer() -> i64 { return 42; }\n")
    write_pkg(
        dapp,
        "dev-app",
        executable=True,
        body="import dev::devlib;\nfn main() -> i64 { return 0; }\nfn test_dev_dependency() -> i64 { return devlib::answer() - 42; }\n",
    )
    with (dapp / "raz.toml").open("a", encoding="utf-8") as f:
        f.write('\n[dev-dependencies]\ndev = "../devlib"\n')
    run([c, "check", "raz.toml"], dapp, env, expect=43)
    run([c, "test", "raz.toml"], dapp, env)

    # Host-target dependency selection.
    target_root = work / "target"
    platform = target_root / "platform"
    tapp = target_root / "app"
    write_pkg(platform, "platform", body="namespace platform;\npublic fn value() -> i64 { return 7; }\n")
    write_pkg(tapp, "target-app", executable=True, body="import platform::platform;\nfn main() -> i64 { return platform::value() - 7; }\n")
    with (tapp / "raz.toml").open("a", encoding="utf-8") as f:
        f.write(f'\n[target.{target}.dependencies]\nplatform = "../platform"\n')
    run([c, "check", "raz.toml"], tapp, env)

    # Commit-pinned Git dependency materialization and cache recreation.
    if shutil.which("git") is None:
        raise RuntimeError("git is required for dependency semantics validation")
    git_root = work / "git"
    gitdep = git_root / "gitdep"
    gapp = git_root / "app"
    write_pkg(gitdep, "gitdep", body="namespace gitdep;\npublic fn value() -> i64 { return 11; }\n")
    run(["git", "init", "-q"], gitdep, env)
    run(["git", "config", "user.email", "raz-test@example.com"], gitdep, env)
    run(["git", "config", "user.name", "Raz Test"], gitdep, env)
    run(["git", "add", "."], gitdep, env)
    run(["git", "commit", "-qm", "fixture"], gitdep, env)
    sha = run(["git", "rev-parse", "HEAD"], gitdep, env).stdout.strip()
    assert len(sha) == 40
    write_pkg(gapp, "git-app", executable=True, body="import gitdep::gitdep;\nfn main() -> i64 { return gitdep::value() - 11; }\n")
    spec = f"git:{gitdep.as_uri()}#{sha}"
    run([c, "add", "gitdep", spec, f"--target={target}"], gapp, env)
    tracking = (gapp / ".raz.git").read_text(encoding="utf-8")
    expected_kind = {"windows": 6, "linux": 7, "macos": 8}[target]
    assert spec in tracking and f"|{expected_kind}" in tracking
    git_manifest = (gapp / "raz.toml").read_text(encoding="utf-8").replace("\\", "/")
    assert f"[target.{target}.dependencies]" in git_manifest and "./target/git/" in git_manifest
    run([c, "check", "raz.toml"], gapp, env)
    shutil.rmtree(gapp / "target" / "git")
    run([c, "check", "raz.toml"], gapp, env)
    assert (gapp / "target" / "git").is_dir(), "ordinary build did not rehydrate target/git"
    run([c, "fetch"], gapp, env)
    git_manifest = (gapp / "raz.toml").read_text(encoding="utf-8")
    assert f"[target.{target}.dependencies]" in git_manifest
    run([c, "check", "raz.toml"], gapp, env)

    # New projects include dependency/feature sections and valid paths.
    scaffold = work / "scaffold"
    run([c, "new", str(scaffold)], work, env)
    generated = (scaffold / "raz.toml").read_text(encoding="utf-8")
    assert "[dependencies]" in generated and "[dev-dependencies]" in generated and "[features]" in generated
    run([c, "check", "raz.toml"], scaffold, env)

    # Project commands discover ./raz.toml when no manifest path is supplied.
    for command in ("check", "test", "run", "build"):
        run([c, command], scaffold, env)

    print("dependency-semantics: PASS (scopes + features + targets + commit-pinned Git + scaffold + default manifest)")
    return 0


if __name__ == "__main__":
    import sys
    raise SystemExit(main())
