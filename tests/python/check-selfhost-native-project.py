#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Exercise a self-hosted compiler as an installed-style project compiler.

This is deliberately end-to-end.  Forge optimizer unit tests catch the SCCP
shape that originally exposed the bug, while this gate proves the generated
Raz compiler still constructs canonical native project paths after self-hosting.
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def run(command: list[str], *, cwd: Path, env: dict[str, str]) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(command, cwd=cwd, env=env, text=True, capture_output=True)
    if result.returncode != 0:
        raise RuntimeError(
            f"command failed ({result.returncode}): {' '.join(command)}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--raz", required=True, type=Path)
    args = parser.parse_args()
    compiler = args.raz.resolve()
    if not compiler.is_file():
        raise SystemExit(f"self-hosted compiler not found: {compiler}")

    with tempfile.TemporaryDirectory(prefix="raz-selfhost-native-") as raw:
        root = Path(raw)
        project = root / "smoke"
        (project / "src").mkdir(parents=True)
        (project / "raz.toml").write_text(
            '[package]\nname = "smoke"\nversion = "0.1.0"\nkind = "executable"\n'
            'source = "src"\nentry = "src/main.rz"\n',
            encoding="utf-8",
            newline="\n",
        )
        (project / "src/main.rz").write_text(
            "fn main() -> i64 {\n    return 7;\n}\n",
            encoding="utf-8",
            newline="\n",
        )
        env = dict(os.environ)
        env["RAZ_HOME"] = str(root / "raz-home")

        suffix = ".exe" if os.name == "nt" else ""
        obj_suffix = ".obj" if os.name == "nt" else ".o"
        for profile, extra in (("debug", []), ("release", ["--release"])):
            run([str(compiler), "build", *extra], cwd=project, env=env)
            artifact = project / "target" / profile / "bin" / f"smoke{suffix}"
            obj = project / "target" / profile / "obj" / f"smoke{obj_suffix}"
            if not artifact.is_file():
                raise RuntimeError(f"missing canonical native artifact: {artifact}")
            if not obj.is_file():
                raise RuntimeError(f"missing canonical native object: {obj}")
            legacy = project / "target" / profile / f"smoke{suffix}"
            if legacy.exists():
                raise RuntimeError(f"legacy flat-profile artifact was produced: {legacy}")
            executed = subprocess.run([str(artifact)], cwd=project, env=env)
            if executed.returncode != 7:
                raise RuntimeError(
                    f"{profile} native artifact returned {executed.returncode}, expected 7: {artifact}"
                )

        # No test-generated build tree should survive TemporaryDirectory cleanup,
        # and no special cwd/install-relative assumptions are permitted here.
        shutil.rmtree(root / "raz-home", ignore_errors=True)

    print("selfhost-native-project: PASS (debug/release obj+bin layout and execution)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
