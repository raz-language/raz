#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0
"""Shared production-compiler test driver.

Pass 114 qualification consumes the already self-hosted production compiler
instead of rebuilding a disposable compiler for every Python test. Source-level
compiler correctness is covered by the self-host/fixed-point gate; these tests
exercise runtime CLI/package behavior against that exact command image.
"""
from __future__ import annotations
from pathlib import Path


def prepare_seed_project(root: Path, project: Path) -> None:
    raise RuntimeError("legacy seed-project preparation was retired; use the qualified production compiler")


def build_test_compiler(root: Path, work: Path, host_compiler: str, linker: str, env: dict[str, str]) -> Path:
    compiler = Path(host_compiler).resolve()
    if not compiler.is_file():
        raise RuntimeError(f"qualified production compiler not found: {compiler}")
    env["RAZ_LINKER"] = str(Path(linker).resolve())
    return compiler
