#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0
"""Verify the self-hosted compiler retained the complete modular native object graph."""
from __future__ import annotations
import argparse
from pathlib import Path
import sys

MODULUS = 281474976710597
DEPENDENCY_PACKAGES = (
    "raz_lexer",
    "raz_parser",
    "raz_query",
    "raz_hir",
    "raz_mir",
    "raz_mir_opt",
    "raz_borrowck",
    "raz_codegen_forge",
    "raz_codegen_llvm",
    "raz_codegen_wasm",
    "raz_codegen_rxe",
    "raz_codegen_web",
    "raz_driver",
)
ROOT_PACKAGE = "raz_compiler"


def package_hash(name: str) -> int:
    value = 17
    for byte in name.encode("utf-8"):
        value = (value * 131 + byte + 1) % MODULUS
    return value


def fail(message: str) -> None:
    print(f"compiler-native-artifact-layout: FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--project-root", required=True)
    parser.add_argument("--profile", default="release")
    args = parser.parse_args()

    project = Path(args.project_root).resolve()
    profile = project / "target" / args.profile
    packages = profile / "packages"
    bin_dir = profile / "bin"
    suffix = ".obj" if sys.platform.startswith("win") else ".o"
    exe = bin_dir / ("raz-compiler.exe" if sys.platform.startswith("win") else "raz-compiler")

    if not exe.is_file() or exe.stat().st_size <= 0:
        fail(f"missing final compiler executable: {exe}")
    if not packages.is_dir():
        fail(f"missing canonical package object directory: {packages}")

    expected_dependency_objects = {
        packages / f"{package_hash(package)}{suffix}" for package in DEPENDENCY_PACKAGES
    }
    missing = sorted(path.name for path in expected_dependency_objects if not path.is_file())
    if missing:
        fail("missing compiler package objects: " + ", ".join(missing))
    for path in expected_dependency_objects:
        if path.stat().st_size <= 0:
            fail(f"empty compiler package object: {path.name}")

    root_dir = packages / str(package_hash(ROOT_PACKAGE))
    root_objects = sorted(root_dir.glob(f"*{suffix}")) if root_dir.is_dir() else []
    if len(root_objects) != 1:
        fail(f"expected exactly one root raz-compiler module object under {root_dir}, found {len(root_objects)}")
    if root_objects[0].stat().st_size <= 0:
        fail(f"empty root compiler module object: {root_objects[0]}")

    all_objects = sorted(packages.rglob(f"*{suffix}"))
    expected_count = len(DEPENDENCY_PACKAGES) + 1
    if len(all_objects) != expected_count:
        fail(f"expected {expected_count} compiler objects, found {len(all_objects)}")

    legacy_obj = profile / "obj" / ("raz-compiler.obj" if sys.platform.startswith("win") else "raz-compiler.o")
    if legacy_obj.exists():
        fail(f"legacy whole-project compiler object survived: {legacy_obj}")
    if (profile / "raz-compiler").exists() or (profile / "raz-compiler.exe").exists():
        fail("legacy flat compiler executable survived outside bin/")
    marker = packages / ".units"
    if not marker.is_file():
        fail("package-unit readiness marker is missing")
    marker_lines = [line.strip() for line in marker.read_text(encoding="utf-8").splitlines() if line.strip()]
    header_fields = marker_lines[0].split() if marker_lines else []
    if len(header_fields) != 3 or not all(field.isdigit() for field in header_fields):
        fail("package-unit readiness marker header is malformed")
    if int(header_fields[2]) <= 0:
        fail("package-unit readiness marker schema must be positive")
    tracked_packages = set()
    tracked_modules = set()
    tracked_fingerprints = set()
    for line in marker_lines[1:]:
        fields = line.split()
        if fields and fields[0] == "F":
            if len(fields) != 5 or not all(field.isdigit() for field in fields[1:]):
                fail(f"malformed package-unit fingerprint record: {line!r}")
            tracked_fingerprints.add(tuple(int(field) for field in fields[1:]))
            continue
        if len(fields) != 2 or fields[0] not in {"P", "M"} or not fields[1].isdigit():
            fail(f"malformed package-unit manifest record: {line!r}")
        if fields[0] == "P":
            tracked_packages.add(int(fields[1]))
        else:
            tracked_modules.add(int(fields[1]))
    expected_hashes = {package_hash(package) for package in DEPENDENCY_PACKAGES}
    if tracked_packages != expected_hashes:
        fail(
            "package-unit manifest dependency set mismatch: "
            f"expected {sorted(expected_hashes)}, found {sorted(tracked_packages)}"
        )
    if int(header_fields[2]) >= 3:
        if not tracked_fingerprints:
            fail("schema-3 package-unit marker is missing native source/interface fingerprints")
        fingerprint_packages = {record[3] for record in tracked_fingerprints}
        if not expected_hashes.issubset(fingerprint_packages) or package_hash(ROOT_PACKAGE) not in fingerprint_packages:
            fail("schema-3 package-unit fingerprints do not cover the complete compiler package graph")
    expected_root_modules = {int(root_objects[0].stem)}
    if tracked_modules != expected_root_modules:
        fail(
            "package-unit manifest root-module set mismatch: "
            f"expected {sorted(expected_root_modules)}, found {sorted(tracked_modules)}"
        )

    print(
        "compiler-native-artifact-layout: PASS "
        f"({len(DEPENDENCY_PACKAGES)} dependency package objects + 1 root module object + bin/raz-compiler)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
