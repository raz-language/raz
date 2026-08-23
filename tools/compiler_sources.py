# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Semantic discovery helpers for the Raz production compiler source tree.

The production compiler is an ordinary module graph.  No checked-in source-order
metadata is allowed: deterministic host/test materialization is derived from the
modules' explicit namespace/import edges.
"""

from __future__ import annotations

import argparse
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
COMPILER_ROOT = ROOT / "compiler"
_NAMESPACE_RE = re.compile(r"(?m)^\s*namespace\s+([A-Za-z_][A-Za-z0-9_]*)\s*;")
_IMPORT_RE = re.compile(r"(?m)^\s*(?:public\s+)?import\s+([A-Za-z_][A-Za-z0-9_]*)\s*;")


def _module_graph(root: Path | None = None) -> tuple[dict[str, Path], dict[str, set[str]]]:
    compiler_root = (root / "compiler") if root is not None else COMPILER_ROOT
    source_root = compiler_root / "src"
    namespace_to_path: dict[str, Path] = {}
    raw_imports: dict[str, set[str]] = {}

    for path in sorted(source_root.rglob("*.rz")):
        text = path.read_text(encoding="utf-8")
        match = _NAMESPACE_RE.search(text)
        if match is None:
            raise RuntimeError(f"compiler module has no namespace: {path.relative_to(compiler_root)}")
        namespace = match.group(1)
        if namespace in namespace_to_path:
            other = namespace_to_path[namespace]
            raise RuntimeError(
                f"duplicate compiler namespace {namespace}: "
                f"{other.relative_to(compiler_root)} and {path.relative_to(compiler_root)}"
            )
        namespace_to_path[namespace] = path
        raw_imports[namespace] = set(_IMPORT_RE.findall(text))

    dependencies = {
        namespace: {item for item in imports if item in namespace_to_path}
        for namespace, imports in raw_imports.items()
    }
    return namespace_to_path, dependencies


def ordered_sources(root: Path | None = None) -> list[Path]:
    """Return a stable discovered source set with the semantic entry point last.

    Raz module imports may contain intentional cycles, so production compilation
    does not impose a topological physical file order.  Host-side qualification
    that needs one synthetic file uses lexical path order only as a deterministic
    serialization detail; semantic imports remain the source of truth.
    """
    namespace_to_path, _dependencies = _module_graph(root)
    paths = sorted(namespace_to_path.values())
    main_candidates = [path for path in paths if path.name == "main.rz" and path.parent.name == "src"]
    if len(main_candidates) != 1:
        raise RuntimeError("compiler/src/main.rz is missing or ambiguous")
    main_path = main_candidates[0]
    paths.remove(main_path)
    paths.append(main_path)
    return paths

def combined_source(root: Path | None = None) -> str:
    chunks: list[str] = []
    for path in ordered_sources(root):
        text = path.read_text(encoding="utf-8")
        chunks.append(text if text.endswith("\n") else text + "\n")
    return "".join(chunks)


def relative_sources(root: Path | None = None) -> list[str]:
    compiler_root = (root / "compiler") if root is not None else COMPILER_ROOT
    return [path.relative_to(compiler_root).as_posix() for path in ordered_sources(root)]


def main() -> int:
    parser = argparse.ArgumentParser(description="Discover Raz compiler modules from semantic imports")
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--materialize", type=Path)
    parser.add_argument("--list", action="store_true")
    args = parser.parse_args()

    if args.materialize is not None:
        args.materialize.parent.mkdir(parents=True, exist_ok=True)
        args.materialize.write_text(combined_source(args.root), encoding="utf-8")
    if args.list:
        for item in relative_sources(args.root):
            print(item)
    if args.materialize is None and not args.list:
        print(len(ordered_sources(args.root)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
