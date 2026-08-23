#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
from pathlib import Path


def prepare_seed_project(root: Path, project: Path) -> None:
    shutil.rmtree(project, ignore_errors=True)
    shutil.copytree(root / "compiler", project, ignore=shutil.ignore_patterns(".raz", "target"))

    # host compiler is the compatibility-pinned host compiler. Runtime/tooling tests only need a
    # Stage-1 production compiler, so mirror bootstrap.py's disposable legacy
    # view rather than teaching the native seed current module/backend syntax.
    for optional_backend in ("wasm", "rxe"):
        shutil.rmtree(project / "src" / "backend" / optional_backend, ignore_errors=True)

    sys.path.insert(0, str(root / "tools"))
    from compiler_sources import relative_sources
    legacy_order = [
        item
        for item in relative_sources(root)
        if not item.startswith("src/backend/wasm/") and not item.startswith("src/backend/rxe/")
    ]

    backend = project / "src" / "driver" / "backend.rz"
    text = backend.read_text(encoding="utf-8")
    text = text.replace(
        "public import raz_compiler_backend_rxe_codegen;",
        "public import raz_compiler_backend_llvm_codegen;",
    )
    text, option_replacements = re.subn(
        r"(?ms)^[ \t]*// --backend=wasm\r?\n.*?(?=^[ \t]*return -1;)",
        "",
        text,
        count=1,
    )
    if option_replacements != 1:
        raise RuntimeError("could not prepare compiler candidate backend option view")

    for backend_kind, emitter in ((2, "emit_wasm_module"), (3, "emit_rxe_module")):
        pattern = (
            rf"(?ms)^[ \t]*if \(backend == {backend_kind}\) \{{\r?\n"
            rf"[ \t]*return {emitter}\([^;]*\);\r?\n"
            rf"[ \t]*\}}\r?\n"
        )
        text, replacements = re.subn(pattern, "", text, count=1)
        if replacements != 1:
            raise RuntimeError(f"could not remove compiler candidate {emitter} dispatch")

    if "emit_wasm_module(" in text or "emit_rxe_module(" in text:
        raise RuntimeError("optional backend dispatch leaked into compiler candidate view")
    backend.write_text(text, encoding="utf-8")

    for source in (project / "src").rglob("*.rz"):
        lines = source.read_text(encoding="utf-8").splitlines()
        lines = [
            line
            for line in lines
            if not line.startswith("namespace raz_compiler_")
            and not line.startswith("public import raz_compiler_")
            and not line.startswith("import raz_compiler_")
        ]
        source.write_text("\n".join(lines) + "\n", encoding="utf-8")

    (project / "source-order.txt").write_text("\n".join(legacy_order) + "\n", encoding="ascii")


def build_test_compiler(root: Path, work: Path, host_compiler: str, linker: str, env: dict[str, str]) -> Path:
    project = work / "compiler"
    prepare_seed_project(root, project)
    env["RAZ_LINKER"] = linker
    p = subprocess.run(
        [host_compiler, "build", str(project), "--profile", "debug", "--force"],
        cwd=root,
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if p.returncode != 0:
        raise RuntimeError(f"could not build compiler candidate test compiler\nstdout:\n{p.stdout}\nstderr:\n{p.stderr}")
    compiler = project / "target" / "debug" / ("raz-compiler.exe" if os.name == "nt" else "raz-compiler")
    if not compiler.is_file():
        raise RuntimeError(f"test compiler was not produced: {compiler}")
    return compiler
