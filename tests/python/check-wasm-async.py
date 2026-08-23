#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Compatibility the resumable async ABI and await lowering for the WASM backend."""
from pathlib import Path

root = Path(__file__).resolve().parents[2]
codegen = (root / "compiler/src/backend/wasm/codegen.rz").read_text(encoding="utf-8")
async_src = (root / "compiler/src/backend/wasm/async.rz").read_text(encoding="utf-8")
globals_src = (root / "compiler/src/backend/wasm/globals.rz").read_text(encoding="utf-8")
order = {path.relative_to(root / 'compiler').as_posix() for path in (root / 'compiler/src').rglob('*.rz')}

required_async = (
    "fn wasm_async_emit_wrapper_body",
    "fn wasm_async_emit_poll_body",
    "fn wasm_async_emit_await",
    "wasm_async_store_frame_state",
    "wasm_async_load_frame_status",
    "wasm_async_i64_store_local",
    "wasm_async_poll_type_index",
    "wasm_async_poll_table_index",
    "wasm_async_emit_spill_register",
    "wasm_async_load_spill",
)
for marker in required_async:
    if marker not in async_src and marker not in codegen:
        raise SystemExit(f"wasm-async: missing resumable-state marker: {marker}")

if "src/backend/wasm/async.rz" not in order:
    raise SystemExit("wasm-async: async.rz missing from semantic compiler source graph")
if "wasm_globals_async_count" not in globals_src or "mir.global_count + 2 + wasm_globals_async_count(hir)" not in globals_src:
    raise SystemExit("wasm-async: current-frame global is not appended to the global section")

for marker in (
    "hir.function_count + wasm_closure_count(mir) + wasm_async_count(hir)",
    "wasm_async_emit_wrapper_body",
    "wasm_async_emit_poll_body",
    "wasm_async_poll_type_index(hir)",
):
    if marker not in codegen:
        raise SystemExit(f"wasm-async: generated poll functions are not integrated: {marker}")

if "opcode == 41" not in codegen:
    raise SystemExit("wasm-async: await opcode is not handled")
if "wasm_opcode_runtime_deferred" not in codegen or "return false;" not in codegen.split("fn wasm_opcode_runtime_deferred", 1)[1].split("fn wasm_opcode_supported", 1)[0]:
    raise SystemExit("wasm-async: await is still classified as runtime-deferred")

# Public async functions are future constructors: their wasm-visible result is
# the i64 frame/future handle even when their eventual Raz result is a float.
type_section = codegen.split("fn wasm_emit_type_section", 1)[1].split("fn wasm_emit_function_section", 1)[0]
if "function_is_async" not in type_section or "wasm_u8(&mut section, 126);" not in type_section:
    raise SystemExit("wasm-async: public async signatures do not expose an i64 future handle")

# Await must preserve the exact instruction PC on suspension and only advance
# after the child future reaches completion.
await_body = codegen.split("fn wasm_async_emit_await", 1)[1].split("fn wasm_async_emit_poll_body", 1)[0]
for marker in ("wasm_async_store_frame_state", "wasm_async_load_frame_status", "wasm_async_load_frame_result"):
    if marker not in await_body:
        raise SystemExit(f"wasm-async: await suspension/resume contract missing: {marker}")

# WASI command async-main is driven by a host-yielding executor rather than
# rejecting the module or spinning continuously.
if "fn wasm_async_emit_wasi_start_body" not in async_src:
    raise SystemExit("wasm-async: missing WASI async-main command driver")
for marker in ("wasm_wasi_emit_poll_delay", "wasm_async_load_frame_status", "wasm_async_load_frame_result"):
    if marker not in async_src:
        raise SystemExit(f"wasm-async: async-main driver missing: {marker}")
validate = codegen.split("fn wasm_validate_module", 1)[1].split("fn wasm_emit_register_get", 1)[0]
if "main_function" in validate and "function_is_async" in validate:
    raise SystemExit("wasm-async: stale async-main rejection remains in validation")
if "wasm_async_emit_wasi_start_body" not in codegen:
    raise SystemExit("wasm-async: code section does not select async WASI _start")

print("wasm-async: PASS (resumable frames + instruction-PC suspension + nested polling + host-yielding async main)")
