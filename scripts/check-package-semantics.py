#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import sys

root = Path(__file__).resolve().parents[1]
checks = {
    "Raz lexer recognizes private": (root / "compiler/src/frontend/lexer.rz", "fn token_is_private("),
    "Raz HIR stores import aliases": (root / "compiler/src/hir/core/model.rz", "namespace_import_aliases"),
    "Raz HIR stores import flags": (root / "compiler/src/hir/core/model.rz", "namespace_import_flags"),
    "Raz HIR resolves aliases": (root / "compiler/src/hir/core/symbols.rz", "fn hir_namespace_resolve_alias("),
    "Raz HIR follows reexports": (root / "compiler/src/hir/core/symbols.rz", "fn hir_namespace_reexports_target("),
    "Raz HIR parses alias syntax": (root / "compiler/src/hir/semantic/comptime.rz", "token_is_as(&builder.scan.input"),
    "Raz HIR stores package provenance": (root / "compiler/src/hir/core/model.rz", "package_segment_hashes"),
    "Raz HIR stores module provenance": (root / "compiler/src/hir/core/model.rz", "module_segment_ids"),
    "Raz HIR stores dependency aliases": (root / "compiler/src/hir/core/model.rz", "package_dependency_aliases"),
    "Raz HIR enforces module-private access": (root / "compiler/src/hir/core/symbols.rz", "fn hir_declaration_accessible("),
    "Raz HIR maps dependency import prefixes": (root / "compiler/src/hir/core/symbols.rz", "hir_find_package_dependency(builder, builder.current_package_hash, first_hash)"),
    "Raz project emits package provenance": (root / "compiler/src/driver/project.rz", "i64 package_prefix[14]"),
    "Raz project emits module provenance": (root / "compiler/src/driver/project.rz", "i64 module_marker[14]"),
    "Raz project emits dependency aliases": (root / "compiler/src/driver/project.rz", "append_project_dependency_alias"),
    "native project import model": (root / "src/bootstrap/compiler/project/project.hpp", "struct ImportSpec"),
    "native package identity conflicts": (root / "src/bootstrap/compiler/project/project.cpp", "conflicting package identity"),
    "native alias ambiguity": (root / "src/bootstrap/compiler/syntax/namespace_lowering.cpp", "ambiguous import alias"),
    "interface v5 compatibility": (root / "src/bootstrap/tools/raz/main.cpp", "raz-interface-v5"),
    "public semantic surface": (root / "src/bootstrap/tools/raz/main.cpp", "SemanticSurface::public_api"),
    "package semantic surface": (root / "src/bootstrap/tools/raz/main.cpp", "SemanticSurface::package_api"),
}
missing = []
for label, (path, needle) in checks.items():
    if path.name == "main.cpp" and path.parent.name == "raz":
        candidates = [path, *sorted((path.parent / "detail").glob("*.hpp"))]
        text = "\n".join(candidate.read_text(encoding="utf-8") for candidate in candidates)
    else:
        text = path.read_text(encoding="utf-8")
    if needle not in text:
        missing.append(f"{label}: missing {needle!r} in {path.relative_to(root)} implementation")

if missing:
    print("package-semantics: FAIL")
    for item in missing:
        print("  " + item)
    sys.exit(1)
print(f"package-semantics: PASS ({len(checks)} structural contracts)")
