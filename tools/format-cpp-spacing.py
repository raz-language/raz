#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Normalize conservative C/C++ vertical spacing in maintained source files.

This formatter intentionally avoids expression/layout rewrites. It only ensures that
adjacent namespace-scope definitions/declarations are separated by one blank line.
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SUFFIXES = {".cpp", ".cc", ".cxx", ".hpp", ".h"}
SKIP_PARTS = {"build", ".git", ".raz", "target", "__pycache__"}

# A line that can plausibly start a namespace-scope declaration/definition. This is
# deliberately broad; inserting whitespace before such a line is semantics-neutral.
DECL_START = re.compile(
    r"^(?:template\s*<|extern\s+\"C\"\s+|(?:static\s+|inline\s+|constexpr\s+|consteval\s+|extern\s+)?"
    r"(?:[A-Za-z_:][A-Za-z0-9_:<>*& ,]*\s+)?[A-Za-z_~][A-Za-z0-9_:~]*\s*\(|"
    r"(?:class|struct|enum|namespace|using|typedef)\b)"
)


def maintained(path: Path) -> bool:
    try:
        rel = path.resolve().relative_to(ROOT)
    except ValueError:
        return True
    return not any(part in SKIP_PARTS for part in rel.parts)


def collect(paths: list[Path]) -> list[Path]:
    out: list[Path] = []
    for path in paths:
        if path.is_dir():
            out.extend(
                p for p in sorted(path.rglob("*"))
                if p.is_file() and p.suffix.lower() in SUFFIXES and maintained(p)
            )
        elif path.suffix.lower() in SUFFIXES:
            out.append(path)
    return out


def brace_delta(line: str) -> int:
    # Good enough for vertical-spacing decisions: strip line comments and quoted
    # literals so braces inside them do not perturb namespace depth.
    code = line.split("//", 1)[0]
    code = re.sub(r'"(?:\\.|[^"\\])*"', '""', code)
    code = re.sub(r"'(?:\\.|[^'\\])*'", "''", code)
    return code.count("{") - code.count("}")


def format_text(text: str) -> str:
    lines = text.splitlines()
    if not lines:
        return text

    # Track brace depth both before and after each line. A function body ending at
    # namespace scope has after_depth(previous) == before_depth(next declaration).
    before_depths: list[int] = []
    after_depths: list[int] = []
    depth = 0
    for line in lines:
        before_depths.append(depth)
        depth += brace_delta(line)
        after_depths.append(depth)

    out: list[str] = []
    for i, line in enumerate(lines):
        stripped = line.strip()
        if stripped and DECL_START.match(stripped):
            j = len(out) - 1
            while j >= 0 and out[j] == "":
                j -= 1
            if j >= 0 and out[j].strip() == "}":
                prev_source = i - 1
                while prev_source >= 0 and not lines[prev_source].strip():
                    prev_source -= 1
                if (
                    prev_source >= 0
                    and after_depths[prev_source] == before_depths[i]
                    and before_depths[i] <= 2
                ):
                    while out and out[-1] == "":
                        out.pop()
                    out.append("")
        out.append(line.rstrip())

    return "\n".join(out).rstrip() + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="+", type=Path)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    changed = False
    for path in collect(args.paths):
        original = path.read_text(encoding="utf-8")
        formatted = format_text(original)
        if formatted == original:
            continue
        changed = True
        if args.check:
            print(f"needs C++ spacing: {path}", file=sys.stderr)
        else:
            path.write_text(formatted, encoding="utf-8")
            print(f"formatted C++ spacing: {path}")
    return 1 if args.check and changed else 0


if __name__ == "__main__":
    raise SystemExit(main())
