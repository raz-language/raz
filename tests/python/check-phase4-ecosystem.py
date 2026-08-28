#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

RAZ_ROOT = Path(__file__).resolve().parents[2]
WORKSPACE = RAZ_ROOT.parent
REGISTRY = WORKSPACE / "packages"

REQUIRED = {
    "archive", "bigint", "cbor", "compression", "crypto", "csv", "datetime",
    "decimal", "encoding", "http-router", "json", "jwt", "msgpack", "multipart",
    "postgres", "protobuf", "regex", "semver", "serde", "sqlite", "testing",
    "toml", "uuid", "websocket", "xml", "yaml",
}

CONTRACTS = {
    "compression/src/lz4.rz": ["public fn compress_bound", "public fn compress(", "public fn decompress("],
    "decimal/src/lib.rz": ["public struct Decimal", "public fn add(", "public fn subtract(", "public fn rescale_exact("],
    "bigint/src/lib.rz": ["public struct BigUint", "public fn add(", "public fn subtract(", "public fn multiply_u64("],
    "xml/src/reader.rz": ["public enum XmlKind", "public fn next(", "public fn next_attribute("],
    "testing/src/fuzz.rz": ["public struct Mutator", "public fn mutate(", "public fn seed_copy("],
}

VERSIONS = {
    "compression": "0.1.0",
    "decimal": "0.1.0",
    "bigint": "0.1.0",
    "xml": "0.1.0",
    "testing": "0.2.0",
}


def fail(message: str) -> int:
    print(f"phase4-ecosystem: FAIL: {message}")
    return 1


def run(*args: str) -> bool:
    result = subprocess.run([sys.executable, *args], cwd=REGISTRY)
    return result.returncode == 0


def main() -> int:
    if not REGISTRY.is_dir():
        # The compiler source archive is intentionally self-contained; the official
        # package registry is maintained as a sibling repository in the full
        # raz-language workspace. Standalone source/release qualification must not
        # fail merely because that optional checkout is absent. Monorepo CI can set
        # RAZ_REQUIRE_REGISTRY_WORKSPACE=1 to make the cross-repository projection
        # an enforced release gate.
        if os.environ.get("RAZ_REQUIRE_REGISTRY_WORKSPACE") == "1":
            return fail("workspace sibling packages/ registry is missing")
        print("phase4-ecosystem: PASS (optional sibling packages/ registry not present; cross-repository projection skipped)")
        return 0
    sources = REGISTRY / "sources"
    present = {path.name for path in sources.iterdir() if path.is_dir()}
    missing = sorted(REQUIRED - present)
    if missing:
        return fail("missing official packages: " + ", ".join(missing))

    for relative, needles in CONTRACTS.items():
        path = sources / relative
        if not path.is_file():
            return fail(f"missing package source {relative}")
        text = path.read_text(encoding="utf-8")
        for needle in needles:
            if needle not in text:
                return fail(f"{relative} is missing contract {needle!r}")

    index = (REGISTRY / "index.txt").read_text(encoding="utf-8")
    for name, version in VERSIONS.items():
        prefix = f"{name} {version} packages/{name}/{version}.dpk "
        if not any(line.startswith(prefix) for line in index.splitlines()):
            return fail(f"registry index does not publish {name}@{version}")
        archive = REGISTRY / "packages" / name / f"{version}.dpk"
        if not archive.is_file():
            return fail(f"immutable archive missing for {name}@{version}")

    checks = [
        ("scripts/validate_registry.py",),
        ("scripts/generate_index.py", "--check"),
        ("scripts/generate_api.py", "--check"),
        ("scripts/generate_search.py", "--check"),
    ]
    for command in checks:
        if not run(*command):
            return fail("registry projection check failed: " + " ".join(command))

    print(f"phase4-ecosystem: PASS ({len(present)} source packages; {len(VERSIONS)} new releases checked)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
