#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0
"""Generate docs/STANDARD-LIBRARY.md from the library source tree.

`raz doc` renders per-declaration documentation for a single file. This produces the
whole-library map that sits above it: every module, its public types and functions,
and the first line of each documentation comment. It is derived from source so the
reference cannot drift from the library.

    python tools/generate-library-index.py
    python tools/generate-library-index.py --check
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LIBRARY = ROOT / "library"
OUTPUT = ROOT / "docs" / "STANDARD-LIBRARY.md"

LAYERS = [
    ("core", "core", "Language and runtime-independent foundations."),
    ("alloc", "alloc", "Allocation-backed collections and memory utilities."),
    ("collections", "collections", "Growable container implementations."),
    ("std", "std", "Operating-system, networking, concurrency, and application APIs."),
]

NAMESPACE = re.compile(r"^namespace\s+([A-Za-z_][\w:]*)\s*[;{]")
DOC_COMMENT = re.compile(r"^\s*///\s?(.*)$")
PUBLIC_ITEM = re.compile(
    r"^public\s+(fn|struct|enum|trait|const)\s+([A-Za-z_]\w*)\s*(.*)$"
)
IMPL_BLOCK = re.compile(r"^impl(?:<[^>]*>)?\s+(?:([\w:]+(?:<[^>]*>)?)\s+for\s+)?([\w:]+(?:<[^>]*>)?)")
METHOD = re.compile(r"^\s{2,}(?:public\s+)?fn\s+([A-Za-z_]\w*)\s*(\([^{]*)")


def signature(kind: str, name: str, rest: str) -> str:
    """Normalize a declaration into a one-line signature."""
    rest = rest.split("{")[0].strip().rstrip(";").strip()
    if kind == "fn":
        return f"fn {name}{rest}"
    if kind == "const":
        return f"const {name} {rest}".strip()
    # Generic parameter lists bind directly to the name: `enum Option<T>`.
    separator = "" if rest.startswith("<") else " "
    return f"{kind} {name}{separator}{rest}".strip()


class Module:
    def __init__(self, namespace: str, path: Path) -> None:
        self.namespace = namespace
        self.path = path
        self.doc: str | None = None
        self.types: list[tuple[str, str | None]] = []
        self.functions: list[tuple[str, str | None]] = []
        self.methods: dict[str, list[str]] = {}

    @property
    def item_count(self) -> int:
        return len(self.types) + len(self.functions) + sum(
            len(v) for v in self.methods.values()
        )


def parse(path: Path) -> Module | None:
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    namespace = None
    for line in lines:
        match = NAMESPACE.match(line.strip())
        if match:
            namespace = match.group(1)
            break
    if namespace is None:
        return None

    module = Module(namespace, path)
    pending: list[str] = []
    current_impl: str | None = None
    impl_depth = 0

    for line in lines:
        doc = DOC_COMMENT.match(line)
        if doc:
            pending.append(doc.group(1).strip())
            continue

        stripped = line.strip()
        if not stripped:
            pending.clear()
            continue

        # An impl body ends when its own brace depth returns to zero. Tracking depth
        # rather than reacting to any '}' keeps nested blocks from ending the impl.
        if current_impl is not None:
            method = METHOD.match(line)
            if method and impl_depth == 1:
                name, params = method.groups()
                module.methods[current_impl].append(
                    f"fn {name}{params.split('{')[0].strip()}"
                )
                pending.clear()
            impl_depth += line.count("{") - line.count("}")
            if impl_depth <= 0:
                current_impl = None
                impl_depth = 0
            continue

        item = PUBLIC_ITEM.match(stripped)
        if item:
            kind, name, rest = item.groups()
            text = pending[0] if pending else None
            entry = (signature(kind, name, rest), text)
            (module.functions if kind == "fn" else module.types).append(entry)
            pending.clear()
            continue

        impl = IMPL_BLOCK.match(stripped)
        if impl:
            trait_name, type_name = impl.groups()
            current_impl = f"{type_name} : {trait_name}" if trait_name else type_name
            module.methods.setdefault(current_impl, [])
            impl_depth = line.count("{") - line.count("}")
            pending.clear()
            continue

        pending.clear()

    if module.item_count == 0:
        return None
    return module


def collect() -> dict[str, list[Module]]:
    layers: dict[str, list[Module]] = {name: [] for name, _, _ in LAYERS}
    for path in sorted(LIBRARY.rglob("*.rz")):
        relative = path.relative_to(LIBRARY)
        layer = relative.parts[0]
        if layer not in layers:
            continue
        module = parse(path)
        if module:
            layers[layer].append(module)
    for modules in layers.values():
        modules.sort(key=lambda m: m.namespace)
    return layers


def render(layers: dict[str, list[Module]]) -> str:
    total_modules = sum(len(m) for m in layers.values())
    total_items = sum(module.item_count for m in layers.values() for module in m)

    out = [
        "# Raz standard library",
        "",
        f"Module map of the {total_modules} standard-library modules and their "
        f"{total_items} public items, grouped by layer.",
        "",
        "This is the whole-library index. For rendered documentation of a single file,",
        "including full documentation comments, run `raz doc <file.rz>`. Design rationale",
        "for the performance-oriented APIs is in",
        "[Standard-library performance](STANDARD-LIBRARY-PERFORMANCE.md).",
        "",
        "> This file is generated by `tools/generate-library-index.py`. Edit the library",
        "> source, then regenerate; do not hand-edit the tables below.",
        "",
        "## Layers",
        "",
        "| Layer | Modules | Items | Scope |",
        "|---|---:|---:|---|",
    ]
    for name, directory, description in LAYERS:
        modules = layers.get(name, [])
        items = sum(module.item_count for module in modules)
        out.append(
            f"| [`{directory}`](#{name}) | {len(modules)} | {items} | {description} |"
        )
    out.append("")

    for name, directory, description in LAYERS:
        modules = layers.get(name, [])
        if not modules:
            continue
        out += [f"## {name}", "", description, ""]
        out += ["| Module | Items |", "|---|---:|"]
        for module in modules:
            # GitHub strips ':' but preserves '_' when building heading anchors.
            anchor = module.namespace.replace("::", "")
            out.append(f"| [`{module.namespace}`](#{anchor}) | {module.item_count} |")
        out.append("")

        for module in modules:
            out += [f"### {module.namespace}", ""]
            out.append(f"`library/{module.path.relative_to(LIBRARY).as_posix()}`")
            out.append("")

            if module.types:
                out += ["**Types**", "", "| Type | Description |", "|---|---|"]
                for text, doc in module.types:
                    out.append(f"| `{text}` | {doc or ''} |")
                out.append("")

            if module.functions:
                out += ["**Functions**", "", "| Function | Description |", "|---|---|"]
                for text, doc in module.functions:
                    out.append(f"| `{text}` | {doc or ''} |")
                out.append("")

            for type_name, methods in module.methods.items():
                if not methods:
                    continue
                out += [f"**Methods on `{type_name}`**", ""]
                out.append("```raz")
                out += methods
                out += ["```", ""]

    return "\n".join(out) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    arguments = parser.parse_args()

    layers = collect()
    if not any(layers.values()):
        print("no library modules found", file=sys.stderr)
        return 1

    expected = render(layers)
    current = OUTPUT.read_text(encoding="utf-8") if OUTPUT.exists() else ""

    if arguments.check:
        if current != expected:
            print(
                "docs/STANDARD-LIBRARY.md is stale; run "
                "python tools/generate-library-index.py",
                file=sys.stderr,
            )
            return 1
        print("library index: PASS")
        return 0

    OUTPUT.write_text(expected, encoding="utf-8")
    modules = sum(len(m) for m in layers.values())
    print(f"wrote {OUTPUT.relative_to(ROOT)} ({modules} modules)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
