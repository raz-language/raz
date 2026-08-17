#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0
"""Generate docs/DIAGNOSTIC-INDEX.md from the compiler's diagnostic call sites.

The diagnostic catalog must never drift from the implementation, so the index is
derived from source rather than maintained by hand. Message templates are
reconstructed from the literal fragments at each call site; interpolated
expressions become <...> placeholders.

    python tools/generate-diagnostic-index.py
    python tools/generate-diagnostic-index.py --check
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE_ROOTS = [ROOT / "src" / "bootstrap", ROOT / "compiler" / "src"]
SOURCE_SUFFIXES = {".cpp", ".hpp", ".rz"}
OUTPUT = ROOT / "docs" / "DIAGNOSTIC-INDEX.md"

CATEGORIES = [
    (0, 999, "lexer", "Source text that cannot be turned into tokens."),
    (1000, 1999, "parser", "Token sequences that are not a well-formed program."),
    (2000, 2999, "semantic", "Programs that parse but violate the language rules."),
    (3000, 3999, "lowering", "Failures while lowering checked programs to MIR."),
    (4000, 4999, "backend", "Failures while turning MIR into target code."),
]

CODE_CALL = re.compile(r'"(D\d{4})"\s*,')
STRING_LITERAL = re.compile(r'"((?:[^"\\]|\\.)*)"')

# Codes whose message text is supplied by the caller rather than written at the
# report site. These cannot be recovered from the call and are described here.
CALLER_SUPPLIED = {
    "D1001": "expected <...> (message names the construct the parser required)",
}


def call_tail(text: str, start: int) -> str:
    """Return the source text of the diagnostic call from `start` to its close paren."""
    depth = 1
    index = start
    while index < len(text) and depth:
        character = text[index]
        if character == '"':
            match = STRING_LITERAL.match(text, index)
            index = match.end() if match else index + 1
            continue
        if character == "(":
            depth += 1
        elif character == ")":
            depth -= 1
        elif character == ";" and depth == 1:
            break
        index += 1
    return text[start:index]


def message_template(tail: str) -> str | None:
    """Rebuild a human-readable template from the literal fragments of a call."""
    pieces: list[str] = []
    position = 0
    gap_pending = False
    for match in STRING_LITERAL.finditer(tail):
        between = tail[position:match.start()]
        if pieces and re.search(r"[A-Za-z_]\w*", between):
            gap_pending = True
        literal = match.group(1).replace('\\"', '"').replace("\\n", " ")
        if literal.strip():
            if gap_pending:
                pieces.append("<...>")
                gap_pending = False
            pieces.append(literal)
        position = match.end()

    # An interpolated expression after the final literal is still part of the message.
    if pieces and re.search(r"[A-Za-z_]\w*", tail[position:]):
        gap_pending = True
    if gap_pending:
        pieces.append("<...>")

    text = "".join(pieces).strip()
    text = re.sub(r"\s+", " ", text)
    # Drop fragments that are pure punctuation or obvious non-messages.
    if len(text) < 6 or not re.search(r"[A-Za-z]{3}", text):
        return None
    # An odd number of quotes means an identifier was being interpolated.
    if text.endswith("'") and text.count("'") % 2:
        text = text + "<...>'"
    return text


def collect() -> dict[str, str]:
    found: dict[str, str] = {}
    for source_root in SOURCE_ROOTS:
        if not source_root.exists():
            continue
        for path in sorted(source_root.rglob("*")):
            if path.suffix not in SOURCE_SUFFIXES:
                continue
            text = path.read_text(encoding="utf-8", errors="replace")
            for match in CODE_CALL.finditer(text):
                code = match.group(1)
                template = message_template(call_tail(text, match.end()))
                if template and code not in found:
                    found[code] = template
    for code, description in CALLER_SUPPLIED.items():
        found.setdefault(code, description)
    return found


def category_of(code: str) -> tuple[str, str]:
    number = int(code[1:])
    for low, high, name, description in CATEGORIES:
        if low <= number <= high:
            return name, description
    return "unknown", ""


def render(found: dict[str, str]) -> str:
    lines = [
        "# Raz diagnostic index",
        "",
        "Every diagnostic the compiler can emit, grouped by category. Codes are stable:",
        "the wording of a message may improve, but a code keeps its meaning so it stays",
        "searchable and usable in `--allow` / `--deny` policy.",
        "",
        "`<...>` marks a value the compiler substitutes into the message, such as a name",
        "or a type. See the [CLI reference](CLI.md#diagnostics) for output formats, warning",
        "policy, and the machine-readable catalog produced by `raz diagnostics`.",
        "",
        "> This file is generated by `tools/generate-diagnostic-index.py`. Edit the compiler",
        "> source, then regenerate; do not hand-edit the tables below.",
        "",
        "## Categories",
        "",
        "| Range | Category | Meaning |",
        "|---|---|---|",
    ]
    for low, high, name, description in CATEGORIES:
        lines.append(f"| `D{low:04d}`-`D{high:04d}` | {name} | {description} |")
    lines.append("")

    for low, high, name, description in CATEGORIES:
        codes = sorted(code for code in found if low <= int(code[1:]) <= high)
        if not codes:
            continue
        lines += [
            f"## {name.capitalize()} (`D{low:04d}`-`D{high:04d}`)",
            "",
            description,
            "",
            "| Code | Message |",
            "|---|---|",
        ]
        for code in codes:
            message = found[code].replace("|", "\\|")
            lines.append(f"| `{code}` | {message} |")
        lines.append("")

    lines += [
        "## Extended explanations",
        "",
        "Codes that come up often enough to deserve more than a message line are explained",
        "in [Common diagnostics](DIAGNOSTICS-EXPLAINED.md).",
        "",
    ]
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    arguments = parser.parse_args()

    found = collect()
    if not found:
        print("no diagnostic codes found; check SOURCE_ROOTS", file=sys.stderr)
        return 1

    expected = render(found)
    current = OUTPUT.read_text(encoding="utf-8") if OUTPUT.exists() else ""

    if arguments.check:
        if current != expected:
            print(
                "docs/DIAGNOSTIC-INDEX.md is stale; run "
                "python tools/generate-diagnostic-index.py",
                file=sys.stderr,
            )
            return 1
        print(f"diagnostic index: PASS ({len(found)} codes)")
        return 0

    OUTPUT.write_text(expected, encoding="utf-8")
    print(f"wrote {OUTPUT.relative_to(ROOT)} ({len(found)} codes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
