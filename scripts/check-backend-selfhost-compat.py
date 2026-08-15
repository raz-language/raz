#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Static compatibility gate for the self-hosted RXE/WebAssembly backends.

The recursive compiler uses semantic modules, so backend code must not rely on
symbols that were only visible accidentally in the old flattened bootstrap
translation unit. This gate also rejects legacy syntax that has previously
survived donor-backend transplants until Stage 2.
"""

from __future__ import annotations

from collections import defaultdict
from pathlib import Path
import re
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "compiler" / "src"
BACKENDS = [SRC / "backend" / "rxe", SRC / "backend" / "wasm"]

module_by_namespace: dict[str, Path] = {}
module_imports: dict[Path, list[str]] = {}
module_defs: dict[Path, set[str]] = {}
module_text: dict[Path, str] = {}
definition_modules: dict[str, set[Path]] = defaultdict(set)

for path in SRC.rglob("*.rz"):
    text = path.read_text(encoding="utf-8")
    ns_match = re.search(r"\bnamespace\s+([A-Za-z_][A-Za-z0-9_]*)\s*;", text)
    if not ns_match:
        continue
    namespace = ns_match.group(1)
    module_by_namespace[namespace] = path
    imports = re.findall(
        r"(?m)^\s*(?:public\s+)?import\s+([A-Za-z_][A-Za-z0-9_]*)\s*;",
        text,
    )
    definitions = set(
        re.findall(
            r"\b(?:public\s+)?(?:extern\s+)?fn\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(",
            text,
        )
    )
    module_imports[path] = imports
    module_defs[path] = definitions
    module_text[path] = text
    for symbol in definitions:
        definition_modules[symbol].add(path)


def import_closure(path: Path) -> set[Path]:
    reachable = {path}
    pending = list(module_imports.get(path, []))
    while pending:
        namespace = pending.pop()
        target = module_by_namespace.get(namespace)
        if target is None or target in reachable:
            continue
        reachable.add(target)
        pending.extend(module_imports.get(target, []))
    return reachable


failures: list[str] = []
backend_files = sorted(path for directory in BACKENDS for path in directory.glob("*.rz"))

# Runtime byte access is a shared compiler primitive. It must be declared once,
# below both driver and backend layers, so Stage 2 sees one ABI symbol and every
# backend import chain can resolve it.
for runtime_symbol in ("raz_rt_load_u8", "raz_rt_store_u8"):
    declarations: list[tuple[Path, int]] = []
    pattern = re.compile(rf"\bextern\s+fn\s+{runtime_symbol}\s*\(")
    for path, text in module_text.items():
        for match in pattern.finditer(text):
            declarations.append((path, text.count("\n", 0, match.start()) + 1))
    if len(declarations) != 1:
        failures.append(
            f"{runtime_symbol}: expected exactly one compiler declaration, found {len(declarations)}"
        )
    elif declarations[0][0] != SRC / "frontend" / "lexer.rz":
        failures.append(
            f"{runtime_symbol}: shared declaration must live in frontend/lexer.rz, found {declarations[0][0].relative_to(ROOT)}"
        )

# Catch old C-style pointer parameter spelling. Current Raz uses typed pointer
# forms such as i64*mut/i64*const or array parameters such as i64[]name.
legacy_pointer = re.compile(
    r"\b(?:i8|i16|i32|i64|i128|isize|usize|u8|u16|u32|u64|u128)\s+\*\s+[A-Za-z_]"
)
for path in backend_files:
    text = module_text[path]
    for match in legacy_pointer.finditer(text):
        line = text.count("\n", 0, match.start()) + 1
        failures.append(f"{path.relative_to(ROOT)}:{line}: legacy C-style pointer spelling")

# Backend output paths supplied by the driver are stage1 arena handles, not
# addresses of scalar handle variables. RXE/Forge/LLVM already use the arena
# writer for this boundary; WebAssembly must do the same or structured Forge
# sees an i64 where the legacy pointer-writing ABI expects ptr.
wasm_codegen = SRC / "backend" / "wasm" / "codegen.rz"
wasm_text = module_text.get(wasm_codegen, "")
if re.search(r"raz_rt_write_ascii_i64\s*\(\s*&mut\s+output_path", wasm_text, re.S):
    failures.append(
        "compiler/src/backend/wasm/codegen.rz: output_path is an arena handle; use raz_rt_stage1_write_ascii instead of borrowing the handle variable"
    )
if "raz_rt_stage1_write_ascii(" not in wasm_text:
    failures.append(
        "compiler/src/backend/wasm/codegen.rz: expected canonical raz_rt_stage1_write_ascii output path"
    )

# Calls to compiler-defined helpers must resolve through the module's explicit
# import closure. Unknown names with no compiler definition are not flagged here
# because language/runtime intrinsics are handled by the compiler itself.
keywords = {
    "if", "while", "for", "match", "return", "sizeof", "alignof", "typeof", "fn", "as"
}
for path in backend_files:
    reachable = import_closure(path)
    text = re.sub(r"//.*", "", module_text[path])
    calls = set(re.findall(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(", text)) - keywords
    for call in sorted(calls):
        providers = definition_modules.get(call)
        if providers and not (providers & reachable):
            locations = ", ".join(str(item.relative_to(ROOT)) for item in sorted(providers))
            failures.append(
                f"{path.relative_to(ROOT)}: `{call}` is not reachable through imports (defined in {locations})"
            )

# Donor revisions used assignment-to-reference syntax for some &mut output
# parameters. Current Raz requires explicit dereference (`*out = ...`).
fn_header = re.compile(r"\bfn\s+([A-Za-z_][A-Za-z0-9_]*)\s*\((.*?)\)[^{;]*\{", re.S)
for path in backend_files:
    text = module_text[path]
    for fn_match in fn_header.finditer(text):
        params = fn_match.group(2)
        mutable_refs = re.findall(r"[^,]*&mut\s+([A-Za-z_][A-Za-z0-9_]*)\b", params)
        if not mutable_refs:
            continue
        start = fn_match.end()
        depth = 1
        cursor = start
        while cursor < len(text) and depth:
            if text[cursor] == "{":
                depth += 1
            elif text[cursor] == "}":
                depth -= 1
            cursor += 1
        body = text[start:cursor - 1]
        for name in mutable_refs:
            assignment = re.compile(
                rf"(?m)(?<![*.A-Za-z0-9_]){re.escape(name)}\s*(?:=|\+=|-=|\|=|&=|\^=)"
            )
            for match in assignment.finditer(body):
                line = text.count("\n", 0, start + match.start()) + 1
                failures.append(
                    f"{path.relative_to(ROOT)}:{line}: &mut parameter `{name}` assigned without explicit dereference"
                )


# Current Raz does not implicitly borrow a local value for a T&/T&mut
# parameter.  A local `Thing value;` must be passed as `&value` or
# `&mut value`; only an already-reference parameter/local may be forwarded
# bare.  This is intentionally a high-confidence call-site audit so Stage 2
# cannot rediscover old donor syntax one function at a time.
def split_top_level(text: str) -> list[str]:
    parts: list[str] = []
    start = 0
    paren = bracket = angle = 0
    for index, char in enumerate(text):
        if char == "(":
            paren += 1
        elif char == ")":
            paren = max(0, paren - 1)
        elif char == "[":
            bracket += 1
        elif char == "]":
            bracket = max(0, bracket - 1)
        elif char == "<":
            angle += 1
        elif char == ">":
            angle = max(0, angle - 1)
        elif char == "," and paren == 0 and bracket == 0 and angle == 0:
            parts.append(text[start:index].strip())
            start = index + 1
    tail = text[start:].strip()
    if tail or parts:
        parts.append(tail)
    return parts


def parameter_mode(parameter: str) -> tuple[str, str] | None:
    match = re.search(r"([A-Za-z_][A-Za-z0-9_]*)\s*$", parameter.strip())
    if not match:
        return None
    prefix = parameter[: match.start(1)]
    if "&mut" in prefix:
        mode = "mutref"
    elif "&" in prefix:
        mode = "ref"
    else:
        mode = "value"
    return match.group(1), mode


signatures: dict[str, list[tuple[str, str] | None]] = {}
for text in module_text.values():
    for match in re.finditer(
        r"\b(?:extern\s+)?fn\s+([A-Za-z_][A-Za-z0-9_]*)\s*\((.*?)\)\s*(?:->\s*[^\{;]+)?\s*[\{;]",
        text,
        re.S,
    ):
        signatures[match.group(1)] = [parameter_mode(item) for item in split_top_level(match.group(2))]


def iter_function_bodies(text: str):
    pattern = re.compile(
        r"\bfn\s+([A-Za-z_][A-Za-z0-9_]*)\s*\((.*?)\)\s*(?:->\s*[^\{;]+)?\s*\{",
        re.S,
    )
    for match in pattern.finditer(text):
        cursor = match.end()
        depth = 1
        while cursor < len(text) and depth:
            if text[cursor] == "{":
                depth += 1
            elif text[cursor] == "}":
                depth -= 1
            cursor += 1
        yield match.group(1), match.group(2), match.end(), text[match.end() : cursor - 1]


def iter_calls(body: str):
    for match in re.finditer(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(", body):
        callee = match.group(1)
        if callee in keywords:
            continue
        open_paren = body.find("(", match.start())
        cursor = open_paren + 1
        depth = 1
        while cursor < len(body) and depth:
            if body[cursor] == "(":
                depth += 1
            elif body[cursor] == ")":
                depth -= 1
            cursor += 1
        if depth == 0:
            yield callee, split_top_level(body[open_paren + 1 : cursor - 1]), match.start()


for path in backend_files:
    text = module_text[path]
    for function_name, raw_parameters, body_start, body in iter_function_bodies(text):
        variables: dict[str, str] = {}
        for item in split_top_level(raw_parameters):
            info = parameter_mode(item)
            if info:
                variables[info[0]] = info[1]

        # High-confidence local declarations. Reference locals are preserved so
        # forwarding an existing reference without another `&` remains valid.
        local_pattern = re.compile(
            r"(?m)(?:^|[;{}]\s*)\s*"
            r"((?:auto|[A-Za-z_][A-Za-z0-9_:]*(?:<[^;=(){}]+>)?(?:\[\])?)(?:&(?:mut)?)?)"
            r"\s+([A-Za-z_][A-Za-z0-9_]*)\s*(?==|;)"
        )
        for local in local_pattern.finditer(body):
            spelling = local.group(1)
            if "&mut" in spelling:
                mode = "mutref"
            elif "&" in spelling:
                mode = "ref"
            else:
                mode = "value"
            variables.setdefault(local.group(2), mode)

        for callee, arguments, call_offset in iter_calls(body):
            expected = signatures.get(callee)
            if not expected:
                continue
            for index, (parameter, argument) in enumerate(zip(expected, arguments), start=1):
                if not parameter or parameter[1] not in {"ref", "mutref"}:
                    continue
                simple = re.fullmatch(r"([A-Za-z_][A-Za-z0-9_]*)", argument.strip())
                if not simple:
                    continue
                variable = simple.group(1)
                actual_mode = variables.get(variable)
                if actual_mode != "value":
                    continue
                line = text.count("\n", 0, body_start + call_offset) + 1
                expected_spelling = f"&mut {variable}" if parameter[1] == "mutref" else f"&{variable}"
                failures.append(
                    f"{path.relative_to(ROOT)}:{line}: `{callee}` argument {index} passes local value "
                    f"`{variable}` to {parameter[1]} parameter; use `{expected_spelling}`"
                )

if failures:
    print("backend-selfhost-compat: FAIL")
    for failure in failures:
        print(f"  {failure}")
    raise SystemExit(1)

print(
    "backend-selfhost-compat: PASS "
    f"({len(backend_files)} backend modules; shared runtime ABI + import visibility + syntax)"
)
