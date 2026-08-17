#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import re
import sys
from pathlib import Path
from urllib.parse import unquote

ROOT = Path(__file__).resolve().parents[2]
LINK_RE = re.compile(r"(?<!!)\[[^\]]+\]\(([^)]+)\)")
SKIP_PREFIXES = ("http://", "https://", "mailto:", "#", "data:")


def maintained_markdown() -> list[Path]:
    files: list[Path] = []
    for path in ROOT.rglob("*.md"):
        rel = path.relative_to(ROOT)
        if rel.parts and rel.parts[0] in {"build", "target", ".git"}:
            continue
        if ".raz" in rel.parts or "__pycache__" in rel.parts:
            continue
        files.append(path)
    return sorted(files)


def main() -> int:
    broken: list[str] = []
    checked = 0
    for source in maintained_markdown():
        text = source.read_text(encoding="utf-8", errors="strict")
        for match in LINK_RE.finditer(text):
            raw = match.group(1).strip()
            if not raw or raw.startswith(SKIP_PREFIXES):
                continue
            # Markdown permits an optional quoted title after a whitespace separator.
            target = raw.split(None, 1)[0].strip("<>")
            target = unquote(target.split("#", 1)[0])
            if not target:
                continue
            # Ignore scheme-like links other than Windows drive paths.
            if ":" in target and not (len(target) >= 2 and target[1] == ":"):
                continue
            resolved = (source.parent / target).resolve()
            try:
                resolved.relative_to(ROOT.resolve())
            except ValueError:
                broken.append(f"{source.relative_to(ROOT)} -> {raw} (escapes repository)")
                continue
            checked += 1
            if not resolved.exists():
                broken.append(f"{source.relative_to(ROOT)} -> {raw}")
    if broken:
        print("Broken repository-relative Markdown links:")
        for item in broken:
            print(f"  {item}")
        return 1
    print(f"documentation-links: PASS ({checked} repository-relative links)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
