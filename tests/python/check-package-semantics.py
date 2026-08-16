#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import sys

root = Path(__file__).resolve().parents[2]
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
    "Raz project recognizes scoped dependencies": (root / "compiler/src/driver/project.rz", "fn project_dependency_section_active("),
    "Raz project recognizes optional features": (root / "compiler/src/driver/project.rz", "fn project_optional_dependency_enabled("),
    "Raz CLI parses feature selection": (root / "compiler/src/driver/cli.rz", "fn cli_features_option("),
    "Raz package rewrites dependency sections": (root / "compiler/src/driver/package.rz", "fn package_rewrite_dependency_in_section("),
    "Raz package tracks Git dependencies": (root / "compiler/src/driver/package.rz", "fn package_git_write_tracking("),
    "Raz package materializes pinned Git commits": (root / "compiler/src/driver/package.rz", "fn package_git_materialize("),
    "Raz registry preserves dependency sections": (root / "compiler/src/driver/registry.rz", "fn registry_tracking_spec_parse("),
    "Raz project recognizes workspace manifests": (root / "compiler/src/driver/project.rz", "if (kind == 10)"),
    "Raz package orchestrates workspaces": (root / "compiler/src/driver/package.rz", "fn package_workspace_command("),
    "Raz workspace recognizes official library packages": (root / "compiler/src/driver/package.rz", "i64 expected[7] = [108, 105, 98, 114, 97, 114, 121]"),
    "Raz workspace lock deduplicates canonical manifests": (root / "compiler/src/driver/package.rz", "fn package_lock_seen_contains("),
    "Raz runtime launches argv in a working directory": (root / "compiler/src/frontend/lexer.rz", "extern fn raz_rt_process_run_argv_cwd("),
    "Raz CLI dispatches workspace operations": (root / "compiler/src/driver/commands.rz", "if (package_workspace_requested(process_argc))"),
    "Raz project expands portable registry paths": (root / "compiler/src/driver/project.rz", "fn path_registry_virtual("),
    "Raz registry writes portable dependency paths": (root / "compiler/src/driver/registry.rz", "fn registry_virtual_path("),
    "Raz registry fetches exact lock entries": (root / "compiler/src/driver/registry.rz", "fn registry_fetch_locked_entry("),
    "Raz registry hydrates transitive package locks": (root / "compiler/src/driver/registry.rz", "fn registry_fetch_package_lock("),
    "Raz registry filters implicit prereleases": (root / "compiler/src/driver/registry.rz", "fn registry_semver_has_prerelease("),
    "Raz package lock records registry source": (root / "compiler/src/driver/package.rz", "fn package_lock_registry_path("),
    "Raz package lock rejects identity conflicts": (root / "compiler/src/driver/package.rz", "fn package_lock_identity_conflicts("),
    "Raz package publication validates portable dependencies": (root / "compiler/src/driver/registry.rz", "fn registry_package_dependencies_publishable("),
    "Raz package archives ignore local registry metadata": (root / "compiler/src/driver/registry_transport.rz", "registry_package_path_ignored"),
    "Raz CLI reports package failures": (root / "compiler/src/driver/commands.rz", "registry_report_status(package_registry_fetch_command())"),
    "Raz CLI exposes dependency tree": (root / "compiler/src/driver/cli.rz", "command == 34"),
    "Raz CLI discovers the current project manifest": (root / "compiler/src/main.rz", "raz_compiler_rt_path_exists_ascii(default_manifest, 8)"),
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
