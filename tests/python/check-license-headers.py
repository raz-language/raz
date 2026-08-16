#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Verify licensing metadata on maintained Raz source/build/script files."""
from __future__ import annotations

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
COPYRIGHT = "Copyright 2026 Mario Vinciguerra"
SPDX = "SPDX-License-Identifier: Apache-2.0"

SOURCE_SUFFIXES = {
    ".rz", ".cpp", ".c", ".hpp", ".h", ".py", ".ps1", ".sh",
    ".bat", ".cmake", ".toml", ".yml", ".yaml",
}
SKIP_PARTS = {"build", ".git", ".raz", "target", "__pycache__"}
SEMANTIC_TARGET_PREFIXES = {
    Path("src/forge/include/forge/target"),
    Path("src/forge/src/target"),
    Path("src/forge/tests/target"),
}


def is_semantic_target(rel: Path) -> bool:
    return any(rel == root or root in rel.parents for root in SEMANTIC_TARGET_PREFIXES)


def is_source(rel: Path) -> bool:
    if any(part in SKIP_PARTS for part in rel.parts) and not is_semantic_target(rel):
        return False
    if rel.name == "CMakeLists.txt" or rel.name.endswith(".cmake.in"):
        return True
    return rel.suffix.lower() in SOURCE_SUFFIXES


def main() -> int:
    missing: list[str] = []
    checked = 0
    for path in sorted(ROOT.rglob("*")):
        if not path.is_file():
            continue
        rel = path.relative_to(ROOT)
        if not is_source(rel):
            continue
        checked += 1
        try:
            head = "\n".join(path.read_text(encoding="utf-8").splitlines()[:10])
        except UnicodeDecodeError:
            missing.append(f"{rel}: not UTF-8 text")
            continue
        if COPYRIGHT not in head or SPDX not in head:
            missing.append(str(rel))

    legal_missing = [name for name in ("LICENSE", "NOTICE") if not (ROOT / name).is_file()]
    if legal_missing:
        missing.extend(f"missing legal file: {name}" for name in legal_missing)

    license_text = (ROOT / "LICENSE").read_text(encoding="utf-8") if (ROOT / "LICENSE").is_file() else ""
    if "Apache License" not in license_text or "Version 2.0" not in license_text:
        missing.append("LICENSE is not the Apache License 2.0 text")

    if missing:
        print("license-headers: FAIL")
        for item in missing:
            print(f"  {item}")
        return 1

    print(f"license-headers: PASS ({checked} maintained source/build/script files)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
