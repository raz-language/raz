#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Central repository path policy.

`target` is overloaded in Raz: it is a generated package-build directory in
ordinary Raz projects, but it is ALSO a semantic source directory in Forge.
All tools that walk/clean/package the repository must use this module so a
broad name-based cleanup can never delete Forge target sources again.
"""
from __future__ import annotations
from pathlib import Path

# These directories are source code despite being named `target`.
SEMANTIC_TARGET_ROOTS = frozenset({
    Path("src/forge/include/forge/target"),
    Path("src/forge/src/target"),
    Path("src/forge/tests/target"),
})

ROOT_GENERATED_DIRS = frozenset({"build", "out", ".git"})
ANYWHERE_GENERATED_DIRS = frozenset({".raz", "__pycache__"})


def _norm(rel: Path | str) -> Path:
    p = Path(rel)
    if p.is_absolute():
        raise ValueError("repository-relative path required")
    return Path(*[part for part in p.parts if part not in ("", ".")])


def is_semantic_target(rel: Path | str) -> bool:
    """True if rel is a semantic Forge `target` root or lives below one."""
    p = _norm(rel)
    return any(p == root or root in p.parents for root in SEMANTIC_TARGET_ROOTS)


def is_generated_dir(rel: Path | str) -> bool:
    """Return whether a repository-relative directory is generated/cache state.

    Crucially, no directory is classified from the basename `target` alone.
    Semantic Forge target trees always win before generated-target handling.
    """
    p = _norm(rel)
    if not p.parts:
        return False
    if is_semantic_target(p):
        return False
    if p.parts[0] in ROOT_GENERATED_DIRS:
        return True
    if any(part in ANYWHERE_GENERATED_DIRS for part in p.parts):
        return True
    # Package/compiler `target` output can occur away from repository root.
    # It is generated only when not under an explicitly semantic target tree.
    if "target" in p.parts:
        return True
    return False


def should_package(rel: Path | str) -> bool:
    p = _norm(rel)
    return not is_generated_dir(p)
