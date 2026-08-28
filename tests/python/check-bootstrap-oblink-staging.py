#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Pin the Windows self-host linker relocation contract.

A self-hosted compiler must be able to perform modular package-unit links from
an arbitrary project working directory. On Windows that means ObLink is staged
beside the compiler and the Raz driver resolves that adjacent executable before
falling back to PATH.
"""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PROJECT = (ROOT / "compiler/src/raz_driver/src/project.rz").read_text(encoding="utf-8")
BOOTSTRAP = (ROOT / "tools/bootstrap.py").read_text(encoding="utf-8")

required_project = (
    "fn project_default_linker_path",
    'project_path_literal(output, capacity, &mut cursor, "/oblink.exe")',
    "raz_compiler_rt_process_arg(0, executable, 8192)",
    "project_default_linker_path(program, 8192, windows)",
)
required_bootstrap = (
    "oblink: Path | None = None",
    'shutil.copy2(oblink, compiler_exe.parent / oblink.name)',
    "stage_compiler_runtime_support(self_host_compiler, runtime, bridge, forge, host_build, oblink)",
    "stage_compiler_runtime_support(verify_compiler, runtime, bridge, forge, host_build, oblink)",
)

missing = [token for token in required_project if token not in PROJECT]
missing += [token for token in required_bootstrap if token not in BOOTSTRAP]
if missing:
    print("bootstrap-oblink-staging: FAIL")
    for token in missing:
        print(f"  missing contract: {token}")
    raise SystemExit(1)

print("bootstrap-oblink-staging: PASS (adjacent ObLink staging + executable-relative Windows linker resolution)")
