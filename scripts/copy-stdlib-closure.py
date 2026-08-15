#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Copy the reachable Raz stdlib module closure for a package entry source.

This keeps runtime/qualification fixtures representative and bounded: they copy
only modules reachable through `import` declarations instead of compiling the
entire standard library for every small test program.
"""
from __future__ import annotations

import argparse
from collections import deque
from pathlib import Path
import re
import shutil

NAMESPACE_RE = re.compile(r"^\s*namespace\s+([A-Za-z_][A-Za-z0-9_:]*)\s*;", re.MULTILINE)
IMPORT_RE = re.compile(r"^\s*import\s+([A-Za-z_][A-Za-z0-9_:]*)\s*;", re.MULTILINE)
STDLIB_ROOTS = ("core::", "alloc::", "collections::", "std::")


def namespace_of(path: Path) -> str:
    match = NAMESPACE_RE.search(path.read_text(encoding="utf-8"))
    if not match:
        raise RuntimeError(f"stdlib module has no namespace declaration: {path}")
    return match.group(1)


def imports_of(path: Path) -> list[str]:
    return IMPORT_RE.findall(path.read_text(encoding="utf-8"))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--library", required=True, type=Path)
    parser.add_argument("--entry", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    library = args.library.resolve()
    entry = args.entry.resolve()
    output = args.output.resolve()
    if not library.is_dir():
        raise SystemExit(f"stdlib-closure: library directory not found: {library}")
    if not entry.is_file():
        raise SystemExit(f"stdlib-closure: entry source not found: {entry}")

    namespace_map: dict[str, Path] = {}
    for module in sorted(library.rglob("*.rz")):
        namespace = namespace_of(module)
        previous = namespace_map.get(namespace)
        if previous is not None:
            raise SystemExit(f"stdlib-closure: duplicate namespace {namespace}: {previous} and {module}")
        namespace_map[namespace] = module

    pending = deque(imports_of(entry))
    visited: set[str] = set()
    copied: list[Path] = []
    while pending:
        namespace = pending.popleft()
        if namespace in visited:
            continue
        visited.add(namespace)
        module = namespace_map.get(namespace)
        if module is None:
            if namespace.startswith(STDLIB_ROOTS):
                raise SystemExit(f"stdlib-closure: unresolved standard-library import {namespace}")
            continue
        relative = module.relative_to(library)
        destination = output / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(module, destination)
        copied.append(relative)
        for dependency in imports_of(module):
            if dependency not in visited:
                pending.append(dependency)

    print(f"stdlib-closure: copied {len(copied)} module(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
