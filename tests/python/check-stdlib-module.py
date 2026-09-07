#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Frontend-check one standard-library module together with its import closure.

A single-file check cannot resolve a module's cross-module references, so a
library module is only checkable alongside everything it imports. Earlier tests either
checked a module alone -- which fails the moment it grows a cross-module
reference -- or carried a hand-written dependency list that went stale as the
library gained modules. The closure is computed from the `import` declarations
instead, so neither can happen again.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import re
import subprocess
import sys

NAMESPACE_RE = re.compile(r"^\s*namespace\s+([A-Za-z_][A-Za-z0-9_:]*)\s*;", re.MULTILINE)
IMPORT_RE = re.compile(r"^\s*(?:public\s+)?import\s+([A-Za-z_][A-Za-z0-9_:]*)\s*;", re.MULTILINE)


def namespace_of(path: Path) -> str | None:
    match = NAMESPACE_RE.search(path.read_text(encoding="utf-8"))
    return match.group(1) if match else None


def imports_of(path: Path) -> list[str]:
    return IMPORT_RE.findall(path.read_text(encoding="utf-8"))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--raz", required=True, type=Path)
    parser.add_argument("--library", required=True, type=Path)
    parser.add_argument("--entry", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    library = args.library.resolve()
    entry = args.entry.resolve()
    if not entry.is_file():
        print(f"stdlib-module: entry not found: {entry}", file=sys.stderr)
        return 1

    # A namespace may be declared by several files, so map to every one of them.
    namespaces: dict[str, list[Path]] = {}
    for module in sorted(library.rglob("*.rz")):
        namespace = namespace_of(module)
        if namespace is not None:
            namespaces.setdefault(namespace, []).append(module)

    ordered: list[Path] = []
    seen: set[Path] = set()

    def visit(path: Path) -> None:
        if path in seen:
            return
        seen.add(path)
        # Dependencies first: the composed file reads top to bottom.
        for namespace in imports_of(path):
            for dependency in namespaces.get(namespace, []):
                visit(dependency)
        ordered.append(path)

    visit(entry)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        "".join(module.read_text(encoding="utf-8") + "\n" for module in ordered),
        encoding="utf-8",
    )

    result = subprocess.run(
        [str(args.raz), "check", str(args.output)],
        text=True, capture_output=True,
    )
    if result.returncode != 0:
        relative = entry.relative_to(library) if entry.is_relative_to(library) else entry
        print(f"stdlib-module: FAIL {relative} ({len(ordered)} module(s) composed)", file=sys.stderr)
        if result.stdout:
            print(result.stdout, file=sys.stderr)
        if result.stderr:
            print(result.stderr, file=sys.stderr)
        return 1

    print(f"stdlib-module: PASS {entry.name} ({len(ordered)} module(s) composed)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
