#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Qualify bootstrap workspace hygiene and canonical target/profile layout."""
from __future__ import annotations

import importlib.util
from pathlib import Path
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("raz_bootstrap", ROOT / "tools" / "bootstrap.py")
assert SPEC is not None and SPEC.loader is not None
bootstrap = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(bootstrap)

expected = {"bin", "lib", "obj", "ir", "modules", "packages"}
if set(bootstrap.PROFILE_OUTPUT_DIRECTORIES) != expected:
    raise SystemExit(f"bootstrap-target-layout: wrong profile directories: {bootstrap.PROFILE_OUTPUT_DIRECTORIES}")

with tempfile.TemporaryDirectory(prefix="raz-bootstrap-layout-") as raw:
    project = Path(raw) / "repro-1"
    stale = [
        project / "host-source-order.txt",
        project / "target" / "host-source-order.txt",
        project / "target" / "cache" / "stage1-diagnostic.txt",
        project / "target" / "release" / "stage1-diagnostic.txt",
    ]
    for path in stale:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("legacy\n", encoding="utf-8")

    removed = bootstrap.remove_legacy_bootstrap_scratch(project)
    if removed != len(stale) or any(path.exists() for path in stale):
        raise SystemExit("bootstrap-target-layout: legacy scratch migration failed")

    layout = bootstrap.ensure_profile_output_layout(project, "release")
    if set(layout) != expected or any(not path.is_dir() for path in layout.values()):
        raise SystemExit("bootstrap-target-layout: canonical profile layout was not created")

    # Migrate the exact flat repro artifact names used by older bootstraps.
    profile_root = project / "target" / "release"
    legacy_object = profile_root / f"compiler{bootstrap.OBJ}"
    legacy_binary = profile_root / f"raz-compiler{bootstrap.EXE}"
    legacy_object.write_bytes(b"object")
    legacy_binary.write_bytes(b"binary")
    removed_flat = bootstrap.remove_legacy_flat_profile_artifacts(project, "release")
    if removed_flat != 2 or legacy_object.exists() or legacy_binary.exists():
        raise SystemExit("bootstrap-target-layout: legacy flat repro artifacts were not migrated")

    canonical_object = layout["obj"] / f"raz-compiler{bootstrap.OBJ}"
    canonical_binary = layout["bin"] / f"raz-compiler{bootstrap.EXE}"
    canonical_object.write_bytes(b"object")
    canonical_binary.write_bytes(b"binary")
    if not canonical_object.is_file() or not canonical_binary.is_file():
        raise SystemExit("bootstrap-target-layout: canonical artifact locations are unusable")


with tempfile.TemporaryDirectory(prefix="raz-bootstrap-final-layout-") as raw:
    root = Path(raw)
    qualification = root / "target" / "bootstrap"
    canonical_profile = root / "canonical" / "target" / "release"
    (canonical_profile / "bin").mkdir(parents=True)
    (canonical_profile / "packages" / "123").mkdir(parents=True)
    (canonical_profile / "bin" / f"raz-compiler{bootstrap.EXE}").write_bytes(b"compiler")
    (canonical_profile / "packages" / f"42{bootstrap.OBJ}").write_bytes(b"dependency")
    (canonical_profile / "packages" / "123" / f"7{bootstrap.OBJ}").write_bytes(b"root")
    (canonical_profile / "packages" / ".units").write_text("1 1 3\n", encoding="utf-8")

    for name in (
        "candidate", "compiler-project", "compiler-package-layout", "repro-1",
        "production-runtime-regressions", "web-static", "web-assets",
    ):
        path = qualification / name
        path.mkdir(parents=True, exist_ok=True)
        (path / "scratch.txt").write_text("scratch\n", encoding="utf-8")

    # Empty profile directories are normal during the build but must not survive
    # successful product finalization.
    (canonical_profile / "obj").mkdir()
    (canonical_profile / "ir").mkdir()
    (canonical_profile / "modules").mkdir()

    staging, staged_profile = bootstrap.prepare_bootstrap_final_tree(qualification, canonical_profile)
    if staged_profile != staging / "release":
        raise SystemExit("bootstrap-target-layout: final staging profile is not release/")
    retained = bootstrap.retain_user_facing_compiler(staged_profile)
    if retained.name != f"raz{bootstrap.EXE}":
        raise SystemExit("bootstrap-target-layout: retained compiler did not use public raz name")
    bootstrap.prune_empty_directories(staged_profile)
    (staging / "BUILD-SUMMARY.txt").write_text("qualified\n", encoding="utf-8")
    bootstrap.promote_bootstrap_final_tree(qualification, staging)

    final_children = {path.name for path in qualification.iterdir()}
    if final_children != {"release", "BUILD-SUMMARY.txt"}:
        raise SystemExit(f"bootstrap-target-layout: successful bootstrap retained scratch: {sorted(final_children)}")
    if not (qualification / "release" / "bin" / f"raz{bootstrap.EXE}").is_file():
        raise SystemExit("bootstrap-target-layout: final production raz executable was not promoted")
    if (qualification / "release" / "bin" / f"raz-compiler{bootstrap.EXE}").exists():
        raise SystemExit("bootstrap-target-layout: internal raz-compiler executable leaked into retained target")
    if any((qualification / "release" / name).exists() for name in ("obj", "ir", "modules")):
        raise SystemExit("bootstrap-target-layout: empty artifact directories survived finalization")
    if not (qualification / "release" / "packages" / f"42{bootstrap.OBJ}").is_file():
        raise SystemExit("bootstrap-target-layout: canonical dependency package object was not promoted")
    if any((qualification / name).exists() for name in ("compiler-project", "repro-1", "web-static")):
        raise SystemExit("bootstrap-target-layout: disposable qualification workspace survived finalization")

# Bootstrap materialization must include semantic compiler inputs only.
input_names = {path.name for path in bootstrap._canonical_compiler_inputs()}
if input_names & bootstrap.BOOTSTRAP_LEGACY_SCRATCH_NAMES:
    raise SystemExit("bootstrap-target-layout: legacy scratch is still a canonical compiler input")

print("bootstrap-target-layout: PASS")
