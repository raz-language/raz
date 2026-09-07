#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Keep the Raz Web stdlib, Wasm lowerer, and JavaScript hosts on one ABI."""

from pathlib import Path
import re
from web_source import web_codegen_source, web_bundle_source
import sys

ROOT = Path(__file__).resolve().parents[2]
STD = ROOT / "library" / "web" / "std"
BROWSER_HOST = ROOT / "compiler" / "src" / "raz_codegen_wasm" / "src" / "wasm" / "browser_host.rz"
WRITER = ROOT / "compiler" / "src" / "raz_codegen_wasm" / "src" / "wasm" / "writer.rz"
WASM_CODEGEN = ROOT / "compiler" / "src" / "raz_codegen_wasm" / "src" / "wasm" / "codegen.rz"
ASYNC = ROOT / "compiler" / "src" / "raz_codegen_wasm" / "src" / "wasm" / "async.rz"
CLIENT_HOST = ROOT / "library" / "web" / "src" / "client_host.rz"
WASI = ROOT / "compiler" / "src" / "raz_codegen_wasm" / "src" / "wasm" / "wasi.rz"
RUNTIME_FUTURE = ROOT / "compiler" / "src" / "raz_codegen_wasm" / "src" / "wasm" / "runtime_future.rz"

extern_re = re.compile(r"extern\s+fn\s+(raz_web_[A-Za-z0-9_]+)\s*\(([^)]*)\)")
stdlib: dict[str, int] = {}
for path in sorted(STD.glob("*.rz")):
    text = path.read_text(encoding="utf-8")
    for match in extern_re.finditer(text):
        name = match.group(1)
        argc = len([arg for arg in match.group(2).split(",") if arg.strip()])
        if name in stdlib:
            raise SystemExit(f"web-browser-abi: FAIL: duplicate stdlib extern {name}")
        stdlib[name] = argc

host = BROWSER_HOST.read_text(encoding="utf-8")
writer = WRITER.read_text(encoding="utf-8")
wasm_codegen = WASM_CODEGEN.read_text(encoding="utf-8")
async_text = ASYNC.read_text(encoding="utf-8")
client = CLIENT_HOST.read_text(encoding="utf-8")
web_codegen = web_codegen_source(ROOT)
wasi_text = WASI.read_text(encoding="utf-8")
runtime_future = RUNTIME_FUTURE.read_text(encoding="utf-8")

logical_re = re.compile(
    r"if\s*\(wasm_browser_([a-z0-9_]+)\(source, hir, function_index\)\)\s*\{\s*return\s+(\d+);\s*\}",
    re.DOTALL,
)
logical: dict[str, int] = {
    "raz_web_" + match.group(1): int(match.group(2)) for match in logical_re.finditer(host)
}

import_re = re.compile(
    r'if\s*\(wasm_browser_import_enabled\((\d+)\)\)\s*\{\s*'
    r'wasm_browser_emit_import_literal\(section, "([a-z0-9_]+)", base(?: \+ (\d+))?\);\s*\}',
    re.DOTALL,
)
imports = {int(m.group(1)): (m.group(2), int(m.group(3) or 0)) for m in import_re.finditer(host)}

runtime_re = re.compile(
    r'if\s*\(wasm_browser_([a-z0-9_]+)\(source, hir, function_index\)\)\s*\{\s*'
    r'return\s+(wasm_browser_emit_import_wrapper[0134]?)\(section, wasm_browser_import_index\((\d+)\)\);\s*\}',
    re.DOTALL,
)
runtime = {"raz_web_" + m.group(1): (m.group(2), int(m.group(3))) for m in runtime_re.finditer(host)}

errors: list[str] = []
if set(logical) != set(stdlib):
    missing = sorted(set(stdlib) - set(logical))
    extra = sorted(set(logical) - set(stdlib))
    if missing:
        errors.append("Wasm lowerer missing: " + ", ".join(missing))
    if extra:
        errors.append("Wasm lowerer has non-stdlib ABI entries: " + ", ".join(extra))

numbers = sorted(logical.values())
expected_numbers = list(range(24, 24 + len(stdlib)))
if numbers != expected_numbers:
    errors.append("logical import IDs are not contiguous from 24")

wrapper_for_arity = {
    0: "wasm_browser_emit_import_wrapper0",
    1: "wasm_browser_emit_import_wrapper1",
    2: "wasm_browser_emit_import_wrapper",
    3: "wasm_browser_emit_import_wrapper3",
    4: "wasm_browser_emit_import_wrapper4",
}
type_for_arity = {0: 1, 1: 2, 2: 0, 3: 3, 4: 4}

for full_name, argc in sorted(stdlib.items()):
    suffix = full_name.removeprefix("raz_web_")
    logical_id = logical.get(full_name)
    if logical_id is None:
        continue
    if imports.get(logical_id) != (suffix, type_for_arity[argc]):
        errors.append(f"wrong/missing host import signature for {full_name}")
    if runtime.get(full_name) != (wrapper_for_arity[argc], logical_id):
        errors.append(f"wrong/missing runtime wrapper for {full_name}")
    token = suffix + ":"
    if token not in client:
        errors.append(f"client host missing {suffix}")
    if token not in web_codegen:
        errors.append(f"generated web host missing {suffix}")

checks = {
    "stdlib browser ABI is nonempty": len(stdlib) > 0,
    "two-lane browser import mask": "wasm_browser_import_mask_high" in writer,
    "full logical range accepted": "logical_import > 100" in writer,
    "four-argument wrapper exists": "fn wasm_browser_emit_import_wrapper4" in host,
    "five browser host Wasm types": "browser_type_count = 5" in wasm_codegen,
    "browser modules omit WASI types": "wasm_wasi_type_count() + browser_type_count" in wasm_codegen,
    "browser import indices pack from zero": "i64 index = 0;" in writer,
    "browser host count omits WASI prefix": "return wasm_browser_import_count_cached;" in writer,
    "async type indexing includes active host types": "wasm_wasi_type_count() + browser_types" in async_text,
    "unknown reachable raz_web externs rejected": 'wasm_host_name_has_literal_prefix(source, hir, function_index, "raz_web_")' in wasm_codegen,
    "browser synthetic start disabled": "if (wasm_browser_mode()) {\n        return 0;" in wasi_text,
    "browser import section excludes WASI": "if (!wasm_browser_mode())" in wasi_text and "wasm_browser_emit_imports(&mut section, base);" in wasi_text,
    "browser future wait avoids poll_oneoff": "if (!wasm_browser_mode()) {\n        wasm_wasi_emit_poll_delay_local" in runtime_future,
    "static host omits WASI namespace": "wasi_snapshot_preview1" not in client,
    "reactive host omits WASI namespace": "wasi_snapshot_preview1" not in web_codegen,
}
for name, ok in checks.items():
    if not ok:
        errors.append(name)

if errors:
    for error in errors:
        print(f"web-browser-abi: FAIL: {error}")
    sys.exit(1)

print(f"web-browser-abi: PASS ({len(stdlib)} stdlib externs synchronized across Wasm + both JS hosts)")
